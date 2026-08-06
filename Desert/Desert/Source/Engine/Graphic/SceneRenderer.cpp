#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Graphic/RenderPhaseRegistry.hpp>
#include <Engine/Graphic/RenderConfig.hpp>
#include <Engine/Core/Application.hpp>
#include <Engine/Core/EngineContext.hpp>
#include <Common/Core/Units.hpp>

#include <Common/Core/Profiler.hpp>

#include <glm/glm.hpp>

#include <chrono>
#include <cmath>

namespace Desert::Graphic
{
    void SceneRenderer::Init()
    {
        // Init may run more than once (e.g. on scene load). Wait for GPU before destroying the old
        // systems — their materials own descriptor pools that may still be in use by in-flight frames.
        Renderer::GetInstance().WaitDeviceIdle();

        // Rebuild from scratch so every system and its framebuffers are recreated consistently —
        // stale systems hold weak_ptrs to framebuffers that get recreated here, which would dangle.
        m_RenderSystems.clear();

        // Pipelines key off framebuffer pointers that are recreated below; drop the cache so systems
        // request fresh pipelines during their Initialize().
        m_PipelineCache.Clear();

        // Ensure the phase registry exists before any system registers custom phases or passes.
        RenderPhaseRegistry::CreateInstance();

        const auto window = EngineContext::GetInstance().GetWindow();
        const auto width  = window ? window->GetWidth() : 1280;
        const auto height = window ? window->GetHeight() : 720;

        // Framebuffer. MSAA applies HERE only: every scene system renders into this target at N
        // samples and the render pass resolves to single-sample for the post stack. Read once —
        // pipelines bake their sample count, so a change applies on the next start.
        FramebufferSpecification fbSpec;
        fbSpec.DebugName = "Composite framebuffer";
        fbSpec.Samples   = static_cast<uint32_t>(
             std::clamp( RenderConfig::MSAASamples.load(), 1, RenderConfig::MaxMSAASamples.load() ) );
        if ( fbSpec.Samples != 1 && fbSpec.Samples != 2 && fbSpec.Samples != 4 && fbSpec.Samples != 8 )
            fbSpec.Samples = 1;
        RenderConfig::MSAASamplesActive = static_cast<int>( fbSpec.Samples );
        fbSpec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::RGBA32F );
        fbSpec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::DEPTH24STENCIL8 );

        m_TargetFramebuffer = Graphic::Framebuffer::Create( fbSpec );
        m_TargetFramebuffer->Resize( width, height );

        // Deferred G-buffer (populated only when SceneSettings::RenderPath == Deferred; allocated always so the
        // toggle is live). GBufferA = Albedo.rgb + Metallic.a (RGBA8F); GBufferB = Normal.rgb + Roughness.a
        // (RGBA32F for banding-free normals — the format enum has no RGBA16F yet); GBufferC = world position.xyz
        // (RGBA32F) so the lighting pass gets point/spot-light distances directly (bulletproof vs depth
        // reconstruction, which is error-prone under the GL-on-Vulkan depth conventions); shared depth.
        FramebufferSpecification gbufferSpec;
        gbufferSpec.DebugName = "GBuffer";
        gbufferSpec.Attachments.Attachments.push_back(
             Core::Formats::ImageFormat::RGBA8F ); // GBufferA Albedo+Metallic
        gbufferSpec.Attachments.Attachments.push_back(
             Core::Formats::ImageFormat::RGBA32F ); // GBufferB Normal+Roughness
        gbufferSpec.Attachments.Attachments.push_back(
             Core::Formats::ImageFormat::RGBA32F ); // GBufferC WorldPosition.xyz
        gbufferSpec.Attachments.Attachments.push_back(
             Core::Formats::ImageFormat::RGBA32F ); // GBufferEmissive (HDR self-illum)
        gbufferSpec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::DEPTH24STENCIL8 );
        m_GBuffer = Graphic::Framebuffer::Create( gbufferSpec );
        m_GBuffer->Resize( width, height );

        // SSAO target: a single-channel-ish AO factor (RGBA8F, AO in .r) the deferred lighting reads.
        FramebufferSpecification ssaoSpec;
        ssaoSpec.DebugName = "SSAO";
        ssaoSpec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::RGBA8F );
        m_SSAOBuffer = Graphic::Framebuffer::Create( ssaoSpec );
        m_SSAOBuffer->Resize( width, height );

        // Scene-colour snapshot (same format as the target) the glass pass samples for refraction.
        FramebufferSpecification copySpec;
        copySpec.DebugName = "SceneColorCopy";
        copySpec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::RGBA32F );
        m_SceneColorCopy = Graphic::Framebuffer::Create( copySpec );
        m_SceneColorCopy->Resize( width, height );

        // Scene systems render into the shared target framebuffer; post-process systems form an
        // explicit chain (Mesh silhouette mask -> Jump Flood outline -> Tonemap).
        RegisterSystem<System::SkyboxRenderer>( "SkyboxSystem", this, m_TargetFramebuffer, m_RenderGraphBuilder );
        RegisterSystem<System::MeshRenderer>( "MeshSystem", this, m_TargetFramebuffer, m_RenderGraphBuilder );
        RegisterSystem<System::JumpFloodOutlineRenderer>( "JumpFloodSystem", this, m_TargetFramebuffer,
                                                          m_RenderGraphBuilder );

        if ( !SP_CAST( System::SkyboxRenderer, m_RenderSystems["SkyboxSystem"] )->Initialize() )
            DESERT_VERIFY( false );

        const auto& meshSystem = SP_CAST( System::MeshRenderer, m_RenderSystems["MeshSystem"] );
        if ( !meshSystem->Initialize() )
            DESERT_VERIFY( false );

        // GPU terrain (tessellated patch grid; opaque geometry, depth-tested with the meshes).
        RegisterSystem<System::TerrainRenderer>( "TerrainSystem", this, m_TargetFramebuffer,
                                                 m_RenderGraphBuilder );
        if ( !SP_CAST( System::TerrainRenderer, m_RenderSystems["TerrainSystem"] )->Initialize() )
            DESERT_VERIFY( false );

        const auto& jumpFloodSystem =
             SP_CAST( System::JumpFloodOutlineRenderer, m_RenderSystems["JumpFloodSystem"] );
        if ( !jumpFloodSystem->Initialize() )
            DESERT_VERIFY( false );

        // Feed the silhouette mask (produced by the mesh system) into the Jump Flood outline.
        jumpFloodSystem->SetMaskFramebuffer( meshSystem->GetSilhouetteMaskFramebuffer() );

        // Tonemap consumes the Jump Flood output (the outlined scene).
        RegisterSystem<System::TonemapRenderer>( "TonemapSystem", this, jumpFloodSystem->GetSystemFramebuffer(),
                                                 m_RenderGraphBuilder );
        const auto& tonemapSystem = SP_CAST( System::TonemapRenderer, m_RenderSystems["TonemapSystem"] );
        if ( !tonemapSystem->Initialize() )
            DESERT_VERIFY( false );

        // Backdrop blur: a blurred snapshot of the scene colour the UI canvas samples for "glass" panels.
        // Runs before the UI phase (which writes into this same target, so it cannot sample it directly).
        RegisterSystem<System::BackdropBlurRenderer>( "BackdropBlurSystem", this, m_TargetFramebuffer,
                                                      m_RenderGraphBuilder );
        if ( const auto& backdropSystem =
                  SP_CAST( System::BackdropBlurRenderer, m_RenderSystems["BackdropBlurSystem"] );
             !backdropSystem->Initialize() )
        {
            LOG_WARN( "Backdrop blur unavailable — UI glass panels will draw as flat tint" );
        }

        // Bloom reads the HDR scene color and produces a compute mip-chain glow that tonemap adds in.
        RegisterSystem<System::BloomRenderer>( "BloomSystem", this, m_TargetFramebuffer, m_RenderGraphBuilder );
        const auto& bloomSystem = SP_CAST( System::BloomRenderer, m_RenderSystems["BloomSystem"] );
        if ( !bloomSystem->Initialize() )
            DESERT_VERIFY( false );

        // SSAO (fullscreen G-buffer -> AO factor). Its target is the dedicated SSAO buffer; deferred lighting
        // reads the result. Runs in the manual chain only when Deferred. Non-fatal.
        RegisterSystem<System::SSAORenderer>( "SSAOSystem", this, m_SSAOBuffer, m_RenderGraphBuilder );
        if ( !SP_CAST( System::SSAORenderer, m_RenderSystems["SSAOSystem"] )->Initialize() )
            LOG_WARN( "[SceneRenderer] SSAO system unavailable." );

        RegisterSystem<System::CopyRenderer>( "SceneColorCopySystem", this, m_SceneColorCopy,
                                              m_RenderGraphBuilder );
        if ( !SP_CAST( System::CopyRenderer, m_RenderSystems["SceneColorCopySystem"] )->Initialize() )
            LOG_WARN( "[SceneRenderer] Scene-color copy system unavailable (glass refraction off)." );

        // GPU particles: compute-simulated billboards drawn in the Transparency phase. Non-fatal.
        RegisterSystem<System::ParticleRenderer>( "ParticleSystem", this, m_TargetFramebuffer,
                                                  m_RenderGraphBuilder );
        if ( !SP_CAST( System::ParticleRenderer, m_RenderSystems["ParticleSystem"] )->Initialize() )
            LOG_WARN( "[SceneRenderer] Particle system unavailable." );

        // Deferred lighting (fullscreen G-buffer shade + debug view). Runs in the manual chain, only when
        // RenderPath == Deferred. Non-fatal if it fails to init (deferred path is simply unavailable).
        RegisterSystem<System::DeferredLightingRenderer>( "DeferredLightingSystem", this, m_TargetFramebuffer,
                                                          m_RenderGraphBuilder );
        if ( !SP_CAST( System::DeferredLightingRenderer, m_RenderSystems["DeferredLightingSystem"] )
                   ->Initialize() )
            LOG_WARN( "[SceneRenderer] Deferred lighting system unavailable." );
        tonemapSystem->SetBloomImage( bloomSystem->GetBloomImage() );

        // Auto-exposure measures the HDR scene luminance into a 1x1 buffer that tonemap reads.
        RegisterSystem<System::AutoExposureRenderer>( "AutoExposureSystem", this, m_TargetFramebuffer,
                                                      m_RenderGraphBuilder );
        const auto& autoExposureSystem =
             SP_CAST( System::AutoExposureRenderer, m_RenderSystems["AutoExposureSystem"] );
        if ( !autoExposureSystem->Initialize() )
            DESERT_VERIFY( false );
        tonemapSystem->SetAutoExposureImage( autoExposureSystem->GetAdaptedLuminanceImage() );

        // FXAA consumes the tonemapped image (LDR). It only runs when SceneSettings.AA == FXAA.
        RegisterSystem<System::FXAARenderer>( "FXAASystem", this, tonemapSystem->GetSystemFramebuffer(),
                                              m_RenderGraphBuilder );
        if ( !SP_CAST( System::FXAARenderer, m_RenderSystems["FXAASystem"] )->Initialize() )
            DESERT_VERIFY( false );

        // SMAA consumes the same tonemapped image. Runs only when SceneSettings.AA == SMAA.
        RegisterSystem<System::SMAARenderer>( "SMAASystem", this, tonemapSystem->GetSystemFramebuffer(),
                                              m_RenderGraphBuilder );
        if ( !SP_CAST( System::SMAARenderer, m_RenderSystems["SMAASystem"] )->Initialize() )
            DESERT_VERIFY( false );

        RebuildRenderGraph();
    }

    SceneRenderer::SceneRenderer()
    {
        // Claimed in creation order: the main viewport is 0, each extra scene view takes the next. A
        // process that opens more views than there are slots folds back to 0 — that is exactly today's
        // behaviour (everyone shares one slot), so it degrades to the status quo rather than breaking.
        static uint32_t s_NextSlot = 0;
        m_RendererSlot             = s_NextSlot < EngineContext::kMaxRendererSlots ? s_NextSlot : 0;
        ++s_NextSlot;
    }

    NO_DISCARD Common::BoolResultStr SceneRenderer::BeginScene( const Desert::Core::Scene& scene )
    {
        // Which renderer is recording, alongside which frame is in flight — see
        // EngineContext::GetActiveRendererSlot. Set FIRST, before anything writes a per-frame resource.
        EngineContext::GetInstance().SetActiveRendererSlot( m_RendererSlot );

        const auto& mainCamera   = scene.GetMainCamera().lock();
        m_SceneInfo.ActiveCamera = mainCamera.get();

        const auto& skyboxSystem = UNIQUE_GET_AS( System::SkyboxRenderer, m_RenderSystems["SkyboxSystem"] );

        skyboxSystem->PrepareCamera( m_SceneInfo.ActiveCamera );

        m_ScenePlaying = scene.IsPlaying(); // grid & other authoring aids hide while the game runs

        const auto& sceneSettings = scene.GetSettings();
        // Selection-outline appearance is NOT read from the scene: it's an editor-only viewport aid pushed
        // each frame via SetOutlineSettings (from EditorPreferences). Runtime builds never push -> the
        // JumpFlood system keeps its defaults, and MeshRenderer::HasOutline() gates whether it draws.

        m_AAMode = sceneSettings.AA;
        // Wireframe is a FORWARD-only debug view (the deferred G-buffer pipeline has no wireframe
        // variant — that's why turning it on in the default Deferred path did nothing). Force forward
        // while it's active so the wireframe pipeline is actually used and the grid composites over it.
        m_RenderPath    = sceneSettings.WireframeMode ? Core::RenderPath::Forward : sceneSettings.RenderingPath;
        m_DeferredDebug = sceneSettings.DeferredDebug;
        m_EnableSSAO    = sceneSettings.EnableSSAO;
        m_EnableSSGI    = sceneSettings.EnableSSGI;

        // Evaluate the scene-global SHARED wind once per frame so every wind-driven renderer (grass now;
        // clouds/hair/cloth next) reads one coherent direction + strength via GetWind(). Direction is a
        // compass heading (degrees) on the XZ plane; Time is monotonic seconds so the sway keeps animating.
        {
            const float       rad       = glm::radians( sceneSettings.WindDirection );
            static const auto windStart = std::chrono::steady_clock::now();
            m_Wind.Direction            = glm::vec2( std::cos( rad ), std::sin( rad ) );
            m_Wind.Strength             = sceneSettings.WindStrength;
            m_Wind.Turbulence           = sceneSettings.WindTurbulence;
            m_Wind.Time = std::chrono::duration<float>( std::chrono::steady_clock::now() - windStart ).count();
        }

        // Grass interactor: the player character bends grass away as it moves. The shader takes ONE influencer,
        // so use the first CharacterController entity (the player); a small array would extend this to NPCs.
        // w = influence radius in WORLD UNITS, and a world unit is a centimetre — this was a bare 1.5f from
        // the metre era, i.e. a radius of one and a half CENTIMETRES, so the grass never bent for anyone.
        {
            const float kGrassInteractRadius     = Common::Units::Metres( 1.5f );
            m_GrassInteractor                    = glm::vec4( 0.0f );
            const auto& reg                      = scene.GetRegistry();
            auto        chars = reg.view<const ECS::CharacterControllerComponent, const ECS::TransformComponent>();
            for ( auto e : chars )
            {
                const auto& tr    = chars.get<const ECS::TransformComponent>( e );
                m_GrassInteractor = glm::vec4( tr.Translation, kGrassInteractRadius );
                break;
            }
        }

        // GPU particles: snapshot the scene's emitters (CPU) here; the compute sim is dispatched in OnUpdate
        // before the render graph, and the billboard pass draws in the Transparency phase.
        UNIQUE_GET_AS( System::ParticleRenderer, m_RenderSystems["ParticleSystem"] )->PrepareFrame( scene );

        UNIQUE_GET_AS( System::TonemapRenderer, m_RenderSystems["TonemapSystem"] )
             ->SetParams( sceneSettings.Exposure, sceneSettings.Gamma );

        UNIQUE_GET_AS( System::MeshRenderer, m_RenderSystems["MeshSystem"] )
             ->SetWireframe( sceneSettings.WireframeMode );
        UNIQUE_GET_AS( System::MeshRenderer, m_RenderSystems["MeshSystem"] )
             ->SetLODEnabled( sceneSettings.MeshLOD );
        UNIQUE_GET_AS( System::MeshRenderer, m_RenderSystems["MeshSystem"] )
             ->SetShadows( sceneSettings.EnableShadows, sceneSettings.ShadowBias,
                           static_cast<int>( sceneSettings.ShadowDebug ), sceneSettings.CascadeSplitLambda );
        UNIQUE_GET_AS( System::MeshRenderer, m_RenderSystems["MeshSystem"] )
             ->SetDebugView( sceneSettings.ShowNormals, sceneSettings.ShowBoundingBoxes,
                             sceneSettings.BoundingBoxColor, sceneSettings.BoundingBoxLineWidth,
                             sceneSettings.LightingDebug );

        // Global texture filter: push into RenderConfig (read by sampler creation). On an actual change,
        // recreate all image samplers so the new filter applies live (no reload).
        const int  desiredFilter = static_cast<int>( sceneSettings.TextureFilterMode );
        const int  desiredAniso  = sceneSettings.Anisotropy;
        const bool filterChanged = RenderConfig::TextureFilter.exchange( desiredFilter ) != desiredFilter;
        const bool anisoChanged  = RenderConfig::AnisotropyLevel.exchange( desiredAniso ) != desiredAniso;
        if ( filterChanged || anisoChanged )
            Renderer::GetInstance().RecreateImageSamplers();

        m_BloomEnabled = sceneSettings.EnableBloom;
        UNIQUE_GET_AS( System::BloomRenderer, m_RenderSystems["BloomSystem"] )
             ->SetThreshold( sceneSettings.BloomThreshold );
        UNIQUE_GET_AS( System::TonemapRenderer, m_RenderSystems["TonemapSystem"] )
             ->SetBloomIntensity( sceneSettings.EnableBloom ? sceneSettings.BloomIntensity : 0.0f );
        UNIQUE_GET_AS( System::TonemapRenderer, m_RenderSystems["TonemapSystem"] )
             ->SetChromaticBloom( sceneSettings.EnableBloom ? sceneSettings.LensDispersion : 0.0f );

        UNIQUE_GET_AS( System::AutoExposureRenderer, m_RenderSystems["AutoExposureSystem"] )
             ->SetParams( sceneSettings.AutoExposureSpeed, sceneSettings.AutoExposureMin,
                          sceneSettings.AutoExposureMax );
        UNIQUE_GET_AS( System::TonemapRenderer, m_RenderSystems["TonemapSystem"] )
             ->SetAutoExposure( sceneSettings.AutoExposure, sceneSettings.AutoExposureKey );

        return BOOLSUCCESS;
    }

    void SceneRenderer::OnUpdate( const UpdateInfo& sceneRenderInfo )
    {
        DESERT_PROFILE_SCOPE( "SceneRenderer::OnUpdate" );

        const auto& skyboxSystem = UNIQUE_GET_AS( System::SkyboxRenderer, m_RenderSystems["SkyboxSystem"] );
        m_DirectionLights        = sceneRenderInfo.DirLights;

        // Bake/rebake the procedural-sky IBL if the sun moved (throttled). Done here — before the render
        // graph records its command buffer — so the heavy compute + device idle stays at a safe boundary.
        {
            DESERT_PROFILE_SCOPE( "Sky: EnsureProceduralEnv" );
            skyboxSystem->EnsureProceduralEnvironment();
        }

        // Recompute CSM cascade matrices once per frame BEFORE the render graph records (intra-phase pass
        // order is nondeterministic, so the cascade passes can't compute them themselves).
        {
            DESERT_PROFILE_SCOPE( "Shadow: UpdateCascades" );
            UNIQUE_GET_AS( System::MeshRenderer, m_RenderSystems["MeshSystem"] )->UpdateCascades();
        }

        {
            DESERT_PROFILE_SCOPE( "ClearMainFramebuffer" );
            ClearMainFramebuffer();
        }

        // GPU grass culling: dispatch the cull compute (outside any render pass) BEFORE the render graph
        // records the grass draw, so the indirect instanceCount + compacted visible list are ready.
        {
            DESERT_PROFILE_SCOPE( "Grass: CullInFrame" );
            UNIQUE_GET_AS( System::TerrainRenderer, m_RenderSystems["TerrainSystem"] )->CullGrassInFrame();
        }

        // Particle simulation compute (outside any render pass) BEFORE the graph records the billboard draw,
        // so the freshly-integrated particle buffer is ready + visible to the vertex stage.
        {
            DESERT_PROFILE_SCOPE( "Particles: SimulateInFrame" );
            UNIQUE_GET_AS( System::ParticleRenderer, m_RenderSystems["ParticleSystem"] )->SimulateInFrame();
        }

        {
            DESERT_PROFILE_SCOPE( "ExecuteRenderGraph" );
            ExecuteRenderGraph();
        }

        // Deferred: fill the G-buffer (manual pass, outside the graph) then shade it (or show a debug channel)
        // into the scene target before the post chain.
        if ( m_RenderPath == Core::RenderPath::Deferred && m_GBuffer )
        {
            DESERT_PROFILE_SCOPE( "Deferred: Lighting" );

            auto* meshRenderer = UNIQUE_GET_AS( System::MeshRenderer, m_RenderSystems["MeshSystem"] );
            meshRenderer->RenderGBufferManual();

            // Resolve the G-buffer depth (static opaque geometry) into the scene target depth. The deferred
            // composite writes only colour, so without this the target depth stays empty and depth-tested
            // overlays (grid, colliders) never get occluded by static meshes — they drew "through" them.
            // Done here, outside any render pass, before the forward-over-composite draws so they too test
            // against real geometry depth.
            if ( m_TargetFramebuffer && m_TargetFramebuffer->GetDepthAttachmentCount() > 0 &&
                 m_GBuffer->GetDepthAttachmentCount() > 0 )
            {
                Renderer::GetInstance().CopyDepthImage( m_GBuffer->GetDepthAttachmentImage().get(),
                                                        m_TargetFramebuffer->GetDepthAttachmentImage().get() );
            }

            glm::vec4 lightDir( 0.0f, -1.0f, 0.0f, 0.0f );
            glm::vec4 lightColor( 1.0f, 0.98f, 0.92f, 3.0f );
            if ( const auto& dl = m_DirectionLights.DirectionLights; !dl.empty() )
            {
                lightDir   = dl[0].Direction;
                lightColor = dl[0].ColorIntensity;
            }
            glm::vec4 cameraPos( 0.0f );
            glm::mat4 viewProj( 1.0f );
            if ( const auto* cam = GetMainCamera() )
            {
                cameraPos = glm::vec4( cam->GetPosition(), 1.0f );
                viewProj  = cam->GetProjectionMatrix() * cam->GetViewMatrix();
            }

            // SSAO first (reads the G-buffer world pos + normal into the AO buffer); the lighting pass below
            // multiplies its ambient term by this. Skipped when disabled (the shader uses AO=1 then).
            std::shared_ptr<Image2D> aoImage;
            if ( m_EnableSSAO )
                if ( auto* ssao = UNIQUE_GET_AS( System::SSAORenderer, m_RenderSystems["SSAOSystem"] ) )
                {
                    ssao->Execute( m_GBuffer, viewProj, cameraPos, /*radius*/ 0.5f, /*bias*/ 0.025f,
                                   /*power*/ 1.5f, /*samples*/ 16 );
                    aoImage = ssao->GetAOImage();
                }

            // Gather the same CSM data the forward material uses so the deferred sun casts identical shadows.
            DeferredShadowInput shadow;
            if ( meshRenderer )
            {
                shadow.CascadeVP            = meshRenderer->GetCascadeViewProj();
                shadow.Count                = System::MeshRenderer::GetCascadeCount();
                shadow.Bias                 = meshRenderer->GetShadowBias();
                shadow.Enabled              = meshRenderer->AreShadowsEnabled();
                shadow.CascadeWorldPerTexel = meshRenderer->GetCascadeWorldPerTexel();
                for ( uint32_t c = 0; c < shadow.Count && c < 4u; ++c )
                {
                    const auto img        = meshRenderer->GetCascadeShadowImage( c );
                    shadow.CascadeMaps[c] = img ? img.get() : nullptr;
                }
            }

            UNIQUE_GET_AS( System::DeferredLightingRenderer, m_RenderSystems["DeferredLightingSystem"] )
                 ->Execute( m_GBuffer, lightDir, lightColor, cameraPos, static_cast<int>( m_DeferredDebug ),
                            GetPointLights(), GetSpotLights(), shadow, aoImage, m_EnableSSGI ? 2.0f : 0.0f,
                            m_EnableSSAO );

            // Custom-shader (generic) meshes have no G-buffer variant — draw them forward OVER
            // the deferred composite (before the glass snapshot so glass refracts them too).
            meshRenderer->RenderGenericManual();

            // Skinned meshes also have no G-buffer variant (the G-buffer pass draws static only), so draw
            // them forward over the composite too — otherwise they only show in the silhouette/outline pass.
            meshRenderer->RenderSkinnedManual();

            // Snapshot the composited opaque scene, then draw the transparent (glass) meshes over it. The
            // snapshot lets the glass sample the scene BEHIND it for refraction without a read+write feedback
            // loop on the target. Uses a dedicated glass material (no double-written per-frame UB ring).
            std::shared_ptr<Image2D> sceneCopy;
            if ( auto* copy = UNIQUE_GET_AS( System::CopyRenderer, m_RenderSystems["SceneColorCopySystem"] ) )
            {
                copy->Execute( m_TargetFramebuffer->GetColorAttachmentImage( 0 ) );
                sceneCopy = copy->GetImage();
            }
            meshRenderer->RenderGlassManual( sceneCopy );
        }

        // Transparent billboards (GPU particles) over the finished opaque scene. Runs in BOTH paths here,
        // after the deferred composite (Deferred) / after the forward geometry graph (Forward), but BEFORE
        // the post chain so particles are tonemapped + bloom'd like everything else. Deferred recorded them
        // inside the graph and the composite painted over them wherever geometry existed (the top-down bug).
        {
            DESERT_PROFILE_SCOPE( "Transparency" );
            ExecuteTransparency();
        }

        // Overdraw debug view: re-rasterize all meshes additively into a heat map over the finished scene
        // color. Path-independent (redraws geometry, ignores the G-buffer), so it runs for Forward too.
        if ( m_DeferredDebug == Core::DeferredDebugMode::Overdraw )
        {
            DESERT_PROFILE_SCOPE( "Debug: Overdraw" );
            UNIQUE_GET_AS( System::MeshRenderer, m_RenderSystems["MeshSystem"] )->RenderOverdrawManual();
        }

        // Debug overlays (bounding boxes, colliders) drawn LAST over the finished scene color — in both
        // paths, but critically in Deferred where the lighting composite above would otherwise cover any
        // debug lines recorded inside the graph. Runs before the post chain so tonemap treats them uniformly.
        {
            DESERT_PROFILE_SCOPE( "Debug: Overlay" );
            ExecuteDebugOverlay();
        }

        // Backdrop blur for UI glass, BEFORE the UI overlay draws into this target. Only when the canvas
        // asked for it last frame: a full mip pyramid every frame for a UI that has no glass is waste,
        // and the one-frame delay is invisible (the first glass frame simply blurs the previous image).
        if ( m_BackdropBlurNeeded )
        {
            DESERT_PROFILE_SCOPE( "UI: BackdropBlur" );
            if ( auto* backdrop =
                      UNIQUE_GET_AS( System::BackdropBlurRenderer, m_RenderSystems["BackdropBlurSystem"] ) )
                backdrop->Execute();
        }

        // UI canvas (Render2D) on top of the finished scene, as a LOAD overlay — see ExecuteUI(). Kept out of
        // the main graph so its CLEAR begin can't wipe the depth the grid/overlays above load.
        {
            DESERT_PROFILE_SCOPE( "UI" );
            ExecuteUI();
        }

        // Explicit post-process chain (runs after the scene graph has produced the scene color and
        // the silhouette mask): Jump Flood outline -> Tonemap.
        {
            DESERT_PROFILE_SCOPE( "PostFX: JumpFlood" );
            const auto& jfa =
                 UNIQUE_GET_AS( System::JumpFloodOutlineRenderer, m_RenderSystems["JumpFloodSystem"] );
            // Skip the JFA step passes when nothing is outlined (sync is handled by render-pass layouts +
            // the EndRenderPass barrier, not by the steps — see JumpFloodOutlineRenderer::Execute).
            jfa->SetOutlineActive(
                 UNIQUE_GET_AS( System::MeshRenderer, m_RenderSystems["MeshSystem"] )->HasOutline() );
            jfa->Execute();
        }

        // Eye adaptation: measure scene luminance into the 1x1 buffer, then point tonemap at the latest
        // (the ping-pong target alternates each frame, so the reference must be refreshed here).
        {
            const auto& autoExp =
                 UNIQUE_GET_AS( System::AutoExposureRenderer, m_RenderSystems["AutoExposureSystem"] );
            autoExp->Execute();
            UNIQUE_GET_AS( System::TonemapRenderer, m_RenderSystems["TonemapSystem"] )
                 ->SetAutoExposureImage( autoExp->GetAdaptedLuminanceImage() );
        }

        // Bloom (HDR scene color -> blurred bright) runs before tonemap, which adds it in.
        if ( m_BloomEnabled )
        {
            DESERT_PROFILE_SCOPE( "PostFX: Bloom" );
            UNIQUE_GET_AS( System::BloomRenderer, m_RenderSystems["BloomSystem"] )->Execute();
        }

        {
            DESERT_PROFILE_SCOPE( "PostFX: Tonemap" );
            UNIQUE_GET_AS( System::TonemapRenderer, m_RenderSystems["TonemapSystem"] )->Execute();
        }

        if ( m_AAMode == Core::AntiAliasingMode::FXAA )
        {
            DESERT_PROFILE_SCOPE( "PostFX: FXAA" );
            UNIQUE_GET_AS( System::FXAARenderer, m_RenderSystems["FXAASystem"] )->Execute();
        }
        else if ( m_AAMode == Core::AntiAliasingMode::SMAA )
        {
            DESERT_PROFILE_SCOPE( "PostFX: SMAA" );
            UNIQUE_GET_AS( System::SMAARenderer, m_RenderSystems["SMAASystem"] )->Execute();
        }

        {
            DESERT_PROFILE_SCOPE( "CompositeRenderPass" );
            CompositeRenderPass();
        }
    }

    NO_DISCARD Common::BoolResultStr SceneRenderer::EndScene()
    {
        UNIQUE_GET_AS( System::MeshRenderer, m_RenderSystems["MeshSystem"] )->ClearQueues();
        UNIQUE_GET_AS( System::TerrainRenderer, m_RenderSystems["TerrainSystem"] )->ClearQueue();

        m_PointLight.PointLights.clear();
        m_SpotLight.SpotLights.clear();

        return BOOLSUCCESS;
    }

    void SceneRenderer::Resize( const uint32_t width, const uint32_t height )
    {
        if ( width == 0 && height == 0 )
            return;
        auto& renderer = Renderer::GetInstance();
        // Ensure all in-flight GPU work is done before destroying/recreating Vulkan resources
        // (framebuffers, descriptor pools). Resize can be triggered from UI code while a command
        // buffer is still recording or submitted frames are executing.
        renderer.WaitDeviceIdle();
        renderer.ResizeWindowEvent( width, height );
        m_TargetFramebuffer->Resize( width, height );
        if ( m_GBuffer )
            m_GBuffer->Resize( width, height );
        if ( m_SSAOBuffer )
            m_SSAOBuffer->Resize( width, height );
        if ( m_SceneColorCopy )
            m_SceneColorCopy->Resize( width, height );

        // Keep the post-process chain framebuffers in lock-step with the scene target.
        if ( const auto& maskFb = UNIQUE_GET_AS( System::MeshRenderer, m_RenderSystems["MeshSystem"] )
                                       ->GetSilhouetteMaskFramebuffer() )
            maskFb->Resize( width, height );

        if ( const auto& overdrawFb =
                  UNIQUE_GET_AS( System::MeshRenderer, m_RenderSystems["MeshSystem"] )->GetOverdrawFramebuffer() )
            overdrawFb->Resize( width, height );

        UNIQUE_GET_AS( System::JumpFloodOutlineRenderer, m_RenderSystems["JumpFloodSystem"] )
             ->OnResize( width, height );
        UNIQUE_GET_AS( System::TonemapRenderer, m_RenderSystems["TonemapSystem"] )->Resize( width, height );
        UNIQUE_GET_AS( System::FXAARenderer, m_RenderSystems["FXAASystem"] )->Resize( width, height );
        UNIQUE_GET_AS( System::SMAARenderer, m_RenderSystems["SMAASystem"] )->Resize( width, height );

        // The backdrop blur pyramid is sized from the target too. Its consumer (the UI pass) reads the
        // image through GetBackdropBlurImage() every frame, so nothing needs re-pointing here.
        if ( auto* backdrop =
                  UNIQUE_GET_AS( System::BackdropBlurRenderer, m_RenderSystems["BackdropBlurSystem"] ) )
            backdrop->Resize( width, height );

        // Bloom recreates its (storage) mip-chain image on resize, so re-point tonemap at the new image.
        const auto& bloomSystem = UNIQUE_GET_AS( System::BloomRenderer, m_RenderSystems["BloomSystem"] );
        bloomSystem->Resize( width, height );
        UNIQUE_GET_AS( System::TonemapRenderer, m_RenderSystems["TonemapSystem"] )
             ->SetBloomImage( bloomSystem->GetBloomImage() );
    }

    // NOTE: if you use rendering without imgui, you may get a black screen! you should start by setting
    // CompositePass!
    void SceneRenderer::CompositeRenderPass()
    {
        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        auto& renderer = Renderer::GetInstance();

        // renderer.BeginSwapChainRenderPass();
        // renderer.EndRenderPass();
    }

    void SceneRenderer::SubmitMesh( const Mesh* mesh, const std::vector<MaterialInstance*>& materialSlots,
                                    const glm::mat4& transform, const RenderSubmissionExtra& extra )
    {
        if ( !mesh || materialSlots.empty() )
        {
            return;
        }
        UNIQUE_GET_AS( System::MeshRenderer, m_RenderSystems["MeshSystem"] )
             ->SubmitMesh( { .Mesh            = (Mesh*)mesh,
                             .Transform       = transform,
                             .MaterialSlots   = &materialSlots,
                             .BoneMatrices    = extra.BoneMatrices,
                             .Outlined        = extra.Outlined,
                             .HiddenSubmeshes = extra.HiddenSubmeshes,
                             .ForcedLOD       = extra.ForcedLOD,
                             .LODBias         = extra.LODBias,
                             .CastShadows     = extra.CastShadows,
                             .ReceiveShadows  = extra.ReceiveShadows } );
    }

    void SceneRenderer::SubmitTerrain( const glm::mat4& transform, float size, int resolution, float heightScale,
                                       float noiseFrequency, int seed, const glm::vec3& layerModes,
                                       Image2D* splatMap, const glm::vec4& grassParams, const glm::vec3& grassTint,
                                       const MaterialOverrides& overrides )
    {
        UNIQUE_GET_AS( System::TerrainRenderer, m_RenderSystems["TerrainSystem"] )
             ->Submit( { .Transform      = transform,
                         .Size           = size,
                         .Resolution     = resolution,
                         .HeightScale    = heightScale,
                         .NoiseFrequency = noiseFrequency,
                         .Seed           = seed,
                         .LayerModes     = layerModes,
                         .SplatMap       = splatMap,
                         .GrassParams    = grassParams,
                         .GrassTint      = grassTint,
                         .Overrides      = overrides } );
    }

    void SceneRenderer::SubmitGenericMesh( const Mesh* mesh, const glm::mat4& transform,
                                           const std::string& shaderName, const MaterialOverrides& overrides,
                                           bool outlined, Image2D* directTexture,
                                           const std::string& directTextureSampler )
    {
        UNIQUE_GET_AS( System::MeshRenderer, m_RenderSystems["MeshSystem"] )
             ->SubmitGenericMesh( { .Mesh                 = const_cast<Mesh*>( mesh ),
                                    .Transform            = transform,
                                    .ShaderName           = shaderName,
                                    .Overrides            = overrides,
                                    .Outlined             = outlined,
                                    .DirectTexture        = directTexture,
                                    .DirectTextureSampler = directTextureSampler } );
    }

    void SceneRenderer::SubmitSlotMaterialMesh( const Mesh* mesh, const glm::mat4& transform, Material* material,
                                                uint64_t visibleSubmeshMask, bool outlined )
    {
        UNIQUE_GET_AS( System::MeshRenderer, m_RenderSystems["MeshSystem"] )
             ->SubmitGenericMesh( { .Mesh               = const_cast<Mesh*>( mesh ),
                                    .Transform          = transform,
                                    .Outlined           = outlined,
                                    .SlotMaterial       = material,
                                    .VisibleSubmeshMask = visibleSubmeshMask } );
    }

    void SceneRenderer::SubmitInstancedMesh( const Mesh* mesh, MaterialInstance* material,
                                             const std::vector<glm::mat4>* transforms )
    {
        UNIQUE_GET_AS( System::MeshRenderer, m_RenderSystems["MeshSystem"] )
             ->SubmitInstancedMesh( { .Mesh       = static_cast<Desert::StaticMesh*>( const_cast<Mesh*>( mesh ) ),
                                      .Material   = material,
                                      .Transforms = transforms } );
    }

    const Environment SceneRenderer::CreateEnvironment( const Common::Filepath& filepath )
    {
        return {}; // EnvironmentManager::Create( filepath );
    }

    void SceneRenderer::SetOutlineSettings( const glm::vec3& color, float width, float smoothness, bool enabled )
    {
        const auto& jumpFloodSystem =
             UNIQUE_GET_AS( System::JumpFloodOutlineRenderer, m_RenderSystems["JumpFloodSystem"] );
        jumpFloodSystem->SetEnabled( enabled );
        jumpFloodSystem->SetOutlineColor( color );
        jumpFloodSystem->SetOutlineWidth( width );
        jumpFloodSystem->SetOutlineSmoothness( smoothness );
    }

    void SceneRenderer::SetEnvironment( const std::shared_ptr<MaterialSkybox>& material, float intensity )
    {
        UNIQUE_GET_AS( System::SkyboxRenderer, m_RenderSystems["SkyboxSystem"] )
             ->PrepareMaterial( material, intensity );
    }

    void SceneRenderer::SetProceduralSky( bool enabled, const glm::vec3& sunDir, float sunIntensity,
                                          float sunDiskRadius, bool bakeNow, const CloudSettings& clouds,
                                          const SkySettings& sky )
    {
        // Inject the SHARED scene wind direction into the cloud config so clouds drift the same heading as
        // grass. The Skybox only authors the per-sky drift RATE (CloudWindSpeed); direction is scene-global.
        CloudSettings windedClouds = clouds;
        windedClouds.WindDir       = m_Wind.Direction;

        UNIQUE_GET_AS( System::SkyboxRenderer, m_RenderSystems["SkyboxSystem"] )
             ->SetProceduralSky( enabled, sunDir, sunIntensity, sunDiskRadius, bakeNow, windedClouds, sky );
    }

    const std::optional<Environment>& SceneRenderer::GetEnvironment()
    {
        return UNIQUE_GET_AS( System::SkyboxRenderer, m_RenderSystems["SkyboxSystem"] )->GetEnvironment();
    }

    std::shared_ptr<Image2D> SceneRenderer::GetShadowCascadeImage( uint32_t cascade )
    {
        return UNIQUE_GET_AS( System::MeshRenderer, m_RenderSystems["MeshSystem"] )
             ->GetCascadeShadowImage( cascade );
    }

    uint32_t SceneRenderer::GetShadowCascadeCount()
    {
        return System::MeshRenderer::GetCascadeCount();
    }

    const std::shared_ptr<Desert::Graphic::Image2D> SceneRenderer::GetFinalImage()
    {
        // FXAA/SMAA write their own framebuffer downstream of tonemap; otherwise tonemap output IS final.
        const char* finalSystem = ( m_AAMode == Core::AntiAliasingMode::FXAA )   ? "FXAASystem"
                                  : ( m_AAMode == Core::AntiAliasingMode::SMAA ) ? "SMAASystem"
                                                                                 : "TonemapSystem";

        return std::static_pointer_cast<System::RenderSystem>( m_RenderSystems[finalSystem] )
             ->GetSystemFramebuffer()
             ->GetColorAttachmentImage();
    }

    void SceneRenderer::AddPointLight( ShaderProtocols::PointLightPayload&& pointLight )
    {
        m_PointLight.PointLights.push_back( std::move( pointLight ) );
    }

    void SceneRenderer::AddSpotLight( ShaderProtocols::SpotLightPayload&& spotLight )
    {
        m_SpotLight.SpotLights.push_back( std::move( spotLight ) );
    }

    namespace
    {
        // Adapts an ExternalPassSpecification to the render-system interface so external passes flow
        // through the same graph build as engine systems (phases, dependencies, same-target merging).
        // The target framebuffer is looked up at RegisterPasses time, so the adapter survives
        // SceneRenderer re-Init (which recreates the framebuffers) until the owner re-registers.
        class ExternalPassSystem final : public IRenderSystem
        {
        public:
            ExternalPassSystem( SceneRenderer* renderer, ExternalPassSpecification&& spec )
                 : m_Renderer( renderer ), m_Spec( std::move( spec ) )
            {
            }

            void RegisterPasses( RenderGraphBuilder& builder ) override
            {
                const auto& target = m_Renderer->GetTargetFramebuffer();
                if ( !target || !m_Spec.Execute )
                    return;

                builder.AddPass(
                     m_Spec.Name, m_Spec.Phase,
                     [this]()
                     {
                         const auto&         target = m_Renderer->GetTargetFramebuffer();
                         ExternalPassContext ctx;
                         ctx.Camera       = m_Renderer->GetMainCamera();
                         ctx.Target       = target.get();
                         ctx.Depth        = target && target->GetDepthAttachmentCount() > 0
                                                 ? target->GetDepthAttachmentImage().get()
                                                 : nullptr;
                         ctx.ScenePlaying = m_Renderer->IsScenePlaying();
                         m_Spec.Execute( ctx );
                     },
                     m_Spec.PipelineSpecification, target, m_Spec.Dependencies );
            }

        private:
            SceneRenderer*            m_Renderer;
            ExternalPassSpecification m_Spec;
        };

        // Namespace external passes so they can never collide with (or evict) an engine system.
        std::string ExternalSystemKey( const std::string& name )
        {
            return "External:" + name;
        }
    } // namespace

    void SceneRenderer::RegisterExternalPass( ExternalPassSpecification&& spec )
    {
        DESERT_VERIFY( !spec.Name.empty() && spec.Execute );
        auto key             = ExternalSystemKey( spec.Name );
        m_RenderSystems[key] = std::make_shared<ExternalPassSystem>( this, std::move( spec ) );
        RebuildRenderGraph();
    }

    void SceneRenderer::UnregisterExternalPass( const std::string& name )
    {
        if ( m_RenderSystems.erase( ExternalSystemKey( name ) ) > 0 )
            RebuildRenderGraph();
    }

    void SceneRenderer::RegisterRenderSystem( const std::string& name, std::shared_ptr<IRenderSystem> system )
    {
        m_RenderSystems[name] = system;
        RebuildRenderGraph();
    }

    void SceneRenderer::UnregisterRenderSystem( const std::string& name )
    {
        m_RenderSystems.erase( name );
        RebuildRenderGraph();
    }

    void SceneRenderer::RebuildRenderGraph()
    {
        m_RenderGraphBuilder.Clear();

        for ( auto& [name, system] : m_RenderSystems )
        {
            system->RegisterPasses( m_RenderGraphBuilder );
        }

        m_RenderGraphBuilder.AddPhaseDependency( RenderPhase::DepthPrePass, RenderPhase::Geometry );
        m_RenderGraphBuilder.AddPhaseDependency( RenderPhase::Sky, RenderPhase::Geometry );
        m_RenderGraphBuilder.AddPhaseDependency( RenderPhase::Geometry, RenderPhase::Outline );
        m_RenderGraphBuilder.AddPhaseDependency( RenderPhase::Geometry, RenderPhase::Lighting );
        m_RenderGraphBuilder.AddPhaseDependency( RenderPhase::Lighting, RenderPhase::PostProcess );

        if ( !m_RenderGraphBuilder.Build() )
        {
            LOG_ERROR( "Failed to build render graph" );
        }
    }

    void SceneRenderer::ExecuteRenderGraph()
    {
        const auto& sortedPasses = m_RenderGraphBuilder.GetSortedPasses();

        auto& renderer = Renderer::GetInstance();

        // Consecutive passes that share the same target framebuffer are merged into a single
        // vkCmdBeginRenderPass/EndRenderPass pair.  The first pass in each group issues a
        // CLEAR begin; subsequent passes in the same group just call ExecuteFunc() inside the
        // already-open render pass.  This lets the skybox draw first and the geometry draw on
        // top without either pass clearing the other's output.
        std::shared_ptr<Framebuffer> currentFb;

        for ( const auto& pass : sortedPasses )
        {
            if ( !pass.CachedRenderPass )
                continue;

            // Debug-phase passes (BB / colliders) are deferred to ExecuteDebugOverlay(), Transparency-phase
            // (particles) to ExecuteTransparency(), and UI-phase (Render2D canvas) to ExecuteUI(): all run
            // AFTER the deferred lighting composite so they aren't painted over by lit geometry, and as LOAD
            // overlays so a CLEAR begin here never wipes the depth later overlays test against (the particle
            // top-down bug / grid-through-meshes).
            if ( pass.Phase == RenderPhase::Debug || pass.Phase == RenderPhase::Transparency ||
                 pass.Phase == RenderPhase::UI )
                continue;

            const auto passFb = pass.CachedRenderPass->GetSpecification().TargetFramebuffer;

            if ( passFb != currentFb )
            {
                DESERT_PROFILE_SCOPE( "RenderPass Begin/End" ); // driver vkCmdBeginRenderPass + layout transitions
                if ( currentFb )
                    renderer.EndRenderPass();
                renderer.BeginRenderPass( pass.CachedRenderPass.get(), true );
                currentFb = passFb;
            }

            DESERT_PROFILE_SCOPE_DYNAMIC( pass.Name.c_str() );

            // Debug-utils region: RenderDoc/Xcode show every graph pass by name in the event tree.
            renderer.BeginDebugLabel( pass.Name.c_str() );
            pass.ExecuteFunc();
            renderer.EndDebugLabel();
        }

        if ( currentFb )
            renderer.EndRenderPass();
    }

    void SceneRenderer::ExecuteDebugOverlay()
    {
        const auto& sortedPasses = m_RenderGraphBuilder.GetSortedPasses();

        auto& renderer = Renderer::GetInstance();

        // LOAD (clearFrame = false) each debug pass over the finished scene color so the lines sit on
        // top of both the forward geometry and the deferred lighting composite. Depth-tested against the
        // target's depth: in Forward that occludes lines behind meshes; in Deferred the target has no
        // opaque-geometry depth (it lives in the G-buffer), so the debug lines read as a clear overlay —
        // exactly what's wanted, since the point is to SEE the boxes even where they hug the mesh.
        std::shared_ptr<Framebuffer> currentFb;
        for ( const auto& pass : sortedPasses )
        {
            if ( !pass.CachedRenderPass || pass.Phase != RenderPhase::Debug )
                continue;

            const auto passFb = pass.CachedRenderPass->GetSpecification().TargetFramebuffer;
            if ( passFb != currentFb )
            {
                if ( currentFb )
                    renderer.EndRenderPass();
                renderer.BeginRenderPass( pass.CachedRenderPass.get(), false );
                currentFb = passFb;
            }

            DESERT_PROFILE_SCOPE_DYNAMIC( pass.Name.c_str() );
            renderer.BeginDebugLabel( pass.Name.c_str() );
            pass.ExecuteFunc();
            renderer.EndDebugLabel();
        }

        if ( currentFb )
            renderer.EndRenderPass();
    }

    void SceneRenderer::ExecuteTransparency()
    {
        const auto& sortedPasses = m_RenderGraphBuilder.GetSortedPasses();

        auto& renderer = Renderer::GetInstance();

        // LOAD (clearFrame = false) each transparency pass over the finished scene color so billboards
        // (particles) sit on top of the composited opaque scene instead of under it. In Deferred the
        // lighting composite runs AFTER the graph, so recording these inside the graph made them show only
        // against the sky (no geometry to overwrite them) and vanish against the ground — see the header.
        // Depth against the target is intentionally NOT tested (the particle pipelines set DepthTest off),
        // so glow/sparks read as "always on top"; a per-emitter occlude toggle can bring it back later.
        std::shared_ptr<Framebuffer> currentFb;
        for ( const auto& pass : sortedPasses )
        {
            if ( !pass.CachedRenderPass || pass.Phase != RenderPhase::Transparency )
                continue;

            const auto passFb = pass.CachedRenderPass->GetSpecification().TargetFramebuffer;
            if ( passFb != currentFb )
            {
                if ( currentFb )
                    renderer.EndRenderPass();
                renderer.BeginRenderPass( pass.CachedRenderPass.get(), false );
                currentFb = passFb;
            }

            DESERT_PROFILE_SCOPE_DYNAMIC( pass.Name.c_str() );
            renderer.BeginDebugLabel( pass.Name.c_str() );
            pass.ExecuteFunc();
            renderer.EndDebugLabel();
        }

        if ( currentFb )
            renderer.EndRenderPass();
    }

    const std::shared_ptr<Desert::Graphic::Image2D>& SceneRenderer::GetBackdropBlurImage() const
    {
        static const std::shared_ptr<Image2D> kNone;
        const auto                            it = m_RenderSystems.find( "BackdropBlurSystem" );
        if ( it == m_RenderSystems.end() )
            return kNone;
        return SP_CAST( System::BackdropBlurRenderer, it->second )->GetImage();
    }

    uint32_t SceneRenderer::GetBackdropBlurMaxLod() const
    {
        const auto it = m_RenderSystems.find( "BackdropBlurSystem" );
        return it == m_RenderSystems.end() ? 0u : SP_CAST( System::BackdropBlurRenderer, it->second )->GetMaxLod();
    }

    void SceneRenderer::ExecuteUI()
    {
        const auto& sortedPasses = m_RenderGraphBuilder.GetSortedPasses();

        auto& renderer = Renderer::GetInstance();

        // LOAD (clearFrame = false) each UI pass over the finished scene so the Render2D canvas sits on top
        // of everything. A CLEAR begin (as the main graph loop uses) would wipe the target's depth, which
        // the grid/debug overlays LOAD afterwards — that clear was the grid-through-meshes regression.
        std::shared_ptr<Framebuffer> currentFb;
        for ( const auto& pass : sortedPasses )
        {
            if ( !pass.CachedRenderPass || pass.Phase != RenderPhase::UI )
                continue;

            const auto passFb = pass.CachedRenderPass->GetSpecification().TargetFramebuffer;
            if ( passFb != currentFb )
            {
                if ( currentFb )
                    renderer.EndRenderPass();
                renderer.BeginRenderPass( pass.CachedRenderPass.get(), false );
                currentFb = passFb;
            }

            DESERT_PROFILE_SCOPE_DYNAMIC( pass.Name.c_str() );
            renderer.BeginDebugLabel( pass.Name.c_str() );
            pass.ExecuteFunc();
            renderer.EndDebugLabel();
        }

        if ( currentFb )
            renderer.EndRenderPass();
    }

    void SceneRenderer::ClearMainFramebuffer()
    {
        auto& renderer = Renderer::GetInstance();

        auto clearRenderPass = RenderPass::Create( {
             .TargetFramebuffer = m_TargetFramebuffer,
             .DebugName         = "ClearTargetFramebuffer",
        } );

        renderer.BeginRenderPass( clearRenderPass.get(), true );
        renderer.EndRenderPass();
    }

} // namespace Desert::Graphic