#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Graphic/RenderPhaseRegistry.hpp>
#include <Engine/Core/Application.hpp>
#include <Engine/Core/EngineContext.hpp>

#include <glm/glm.hpp>

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

        // Ensure the phase registry exists before any system registers custom phases or passes.
        RenderPhaseRegistry::CreateInstance();

        const auto window = EngineContext::GetInstance().GetWindow();
        const auto width  = window ? window->GetWidth() : 1280;
        const auto height = window ? window->GetHeight() : 720;


        // Framebuffer
        FramebufferSpecification fbSpec;
        fbSpec.DebugName = "Composite framebuffer";
        fbSpec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::RGBA32F );
        fbSpec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::DEPTH24STENCIL8 );

        m_TargetFramebuffer = Graphic::Framebuffer::Create( fbSpec );
        m_TargetFramebuffer->Resize( width, height );

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

        const auto& jumpFloodSystem =
             SP_CAST( System::JumpFloodOutlineRenderer, m_RenderSystems["JumpFloodSystem"] );
        if ( !jumpFloodSystem->Initialize() )
            DESERT_VERIFY( false );

        // Feed the silhouette mask (produced by the mesh system) into the Jump Flood outline.
        jumpFloodSystem->SetMaskFramebuffer( meshSystem->GetSilhouetteMaskFramebuffer() );

        // Tonemap consumes the Jump Flood output (the outlined scene).
        RegisterSystem<System::TonemapRenderer>( "TonemapSystem", this,
                                                 jumpFloodSystem->GetSystemFramebuffer(), m_RenderGraphBuilder );
        const auto& tonemapSystem = SP_CAST( System::TonemapRenderer, m_RenderSystems["TonemapSystem"] );
        if ( !tonemapSystem->Initialize() )
            DESERT_VERIFY( false );

        // Bloom reads the HDR scene color and produces a blurred bright buffer that tonemap adds in.
        RegisterSystem<System::BloomRenderer>( "BloomSystem", this, m_TargetFramebuffer, m_RenderGraphBuilder );
        const auto& bloomSystem = SP_CAST( System::BloomRenderer, m_RenderSystems["BloomSystem"] );
        if ( !bloomSystem->Initialize() )
            DESERT_VERIFY( false );
        tonemapSystem->SetBloomFramebuffer( bloomSystem->GetSystemFramebuffer() );

        // Auto-exposure measures the HDR scene luminance into a 1x1 buffer that tonemap reads.
        RegisterSystem<System::AutoExposureRenderer>( "AutoExposureSystem", this, m_TargetFramebuffer,
                                                      m_RenderGraphBuilder );
        const auto& autoExposureSystem = SP_CAST( System::AutoExposureRenderer, m_RenderSystems["AutoExposureSystem"] );
        if ( !autoExposureSystem->Initialize() )
            DESERT_VERIFY( false );
        tonemapSystem->SetAutoExposureFramebuffer( autoExposureSystem->GetAdaptedLuminanceFramebuffer() );

        // FXAA consumes the tonemapped image (LDR). It only runs when SceneSettings.AA == FXAA.
        RegisterSystem<System::FXAARenderer>( "FXAASystem", this, tonemapSystem->GetSystemFramebuffer(),
                                              m_RenderGraphBuilder );
        if ( !SP_CAST( System::FXAARenderer, m_RenderSystems["FXAASystem"] )->Initialize() )
            DESERT_VERIFY( false );

        RebuildRenderGraph();
    }

    NO_DISCARD Common::BoolResultStr SceneRenderer::BeginScene( const Desert::Core::Scene& scene )
    {
        const auto& mainCamera   = scene.GetMainCamera().lock();
        m_SceneInfo.ActiveCamera = mainCamera.get();

        const auto& skyboxSystem = UNIQUE_GET_AS( System::SkyboxRenderer, m_RenderSystems["SkyboxSystem"] );

        skyboxSystem->PrepareCamera( m_SceneInfo.ActiveCamera );

        const auto& sceneSettings    = scene.GetSettings();
        const auto& jumpFloodSystem  = UNIQUE_GET_AS( System::JumpFloodOutlineRenderer,
                                                     m_RenderSystems["JumpFloodSystem"] );
        jumpFloodSystem->SetEnabled( sceneSettings.EnableOutline );
        jumpFloodSystem->SetOutlineColor( sceneSettings.OutlineColor );
        jumpFloodSystem->SetOutlineWidth( sceneSettings.OutlineWidth );
        jumpFloodSystem->SetOutlineSmoothness( sceneSettings.OutlineSmoothness );

        m_AAMode = sceneSettings.AA;

        UNIQUE_GET_AS( System::TonemapRenderer, m_RenderSystems["TonemapSystem"] )
             ->SetParams( sceneSettings.Exposure, sceneSettings.Gamma );

        UNIQUE_GET_AS( System::MeshRenderer, m_RenderSystems["MeshSystem"] )
             ->SetWireframe( sceneSettings.WireframeMode );

        m_BloomEnabled = sceneSettings.EnableBloom;
        UNIQUE_GET_AS( System::BloomRenderer, m_RenderSystems["BloomSystem"] )
             ->SetThreshold( sceneSettings.BloomThreshold );
        UNIQUE_GET_AS( System::TonemapRenderer, m_RenderSystems["TonemapSystem"] )
             ->SetBloomIntensity( sceneSettings.EnableBloom ? sceneSettings.BloomIntensity : 0.0f );

        UNIQUE_GET_AS( System::AutoExposureRenderer, m_RenderSystems["AutoExposureSystem"] )
             ->SetParams( sceneSettings.AutoExposureSpeed, sceneSettings.AutoExposureMin,
                          sceneSettings.AutoExposureMax );
        UNIQUE_GET_AS( System::TonemapRenderer, m_RenderSystems["TonemapSystem"] )
             ->SetAutoExposure( sceneSettings.AutoExposure, sceneSettings.AutoExposureKey );

        return BOOLSUCCESS;
    }

    void SceneRenderer::OnUpdate( const UpdateInfo& sceneRenderInfo )
    {
        const auto& skyboxSystem = UNIQUE_GET_AS( System::SkyboxRenderer, m_RenderSystems["SkyboxSystem"] );
        m_DirectionLights        = sceneRenderInfo.DirLights;

        ClearMainFramebuffer();
        ExecuteRenderGraph();

        // Explicit post-process chain (runs after the scene graph has produced the scene color and
        // the silhouette mask): Jump Flood outline -> Tonemap.
        UNIQUE_GET_AS( System::JumpFloodOutlineRenderer, m_RenderSystems["JumpFloodSystem"] )->Execute();

        // Eye adaptation: measure scene luminance into the 1x1 buffer, then point tonemap at the latest
        // (the ping-pong target alternates each frame, so the reference must be refreshed here).
        {
            const auto& autoExp = UNIQUE_GET_AS( System::AutoExposureRenderer, m_RenderSystems["AutoExposureSystem"] );
            autoExp->Execute();
            UNIQUE_GET_AS( System::TonemapRenderer, m_RenderSystems["TonemapSystem"] )
                 ->SetAutoExposureFramebuffer( autoExp->GetAdaptedLuminanceFramebuffer() );
        }

        // Bloom (HDR scene color -> blurred bright) runs before tonemap, which adds it in.
        if ( m_BloomEnabled )
            UNIQUE_GET_AS( System::BloomRenderer, m_RenderSystems["BloomSystem"] )->Execute();

        UNIQUE_GET_AS( System::TonemapRenderer, m_RenderSystems["TonemapSystem"] )->Execute();

        if ( m_AAMode == Core::AntiAliasingMode::FXAA )
            UNIQUE_GET_AS( System::FXAARenderer, m_RenderSystems["FXAASystem"] )->Execute();

        CompositeRenderPass();
    }

    NO_DISCARD Common::BoolResultStr SceneRenderer::EndScene()
    {
        UNIQUE_GET_AS( System::MeshRenderer, m_RenderSystems["MeshSystem"] )->ClearQueues();

        m_PointLight.PointLights.clear();

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

        // Keep the post-process chain framebuffers in lock-step with the scene target.
        if ( const auto& maskFb =
                  UNIQUE_GET_AS( System::MeshRenderer, m_RenderSystems["MeshSystem"] )->GetSilhouetteMaskFramebuffer() )
            maskFb->Resize( width, height );

        UNIQUE_GET_AS( System::JumpFloodOutlineRenderer, m_RenderSystems["JumpFloodSystem"] )
             ->OnResize( width, height );
        UNIQUE_GET_AS( System::TonemapRenderer, m_RenderSystems["TonemapSystem"] )->Resize( width, height );
        UNIQUE_GET_AS( System::FXAARenderer, m_RenderSystems["FXAASystem"] )->Resize( width, height );
        UNIQUE_GET_AS( System::BloomRenderer, m_RenderSystems["BloomSystem"] )->Resize( width, height );
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

    void SceneRenderer::SubmitMesh( const Mesh* mesh, const std::vector<MaterialInstance*> materialSlots,
                                    const glm::mat4& transform, const RenderSubmissionExtra& extra )
    {
        if ( !mesh || materialSlots.empty() )
        {
            return;
        }
        UNIQUE_GET_AS( System::MeshRenderer, m_RenderSystems["MeshSystem"] )
             ->SubmitMesh( { .Mesh          = (Mesh*)mesh,
                             .Transform     = transform,
                             .MaterialSlots = materialSlots,
                             .BoneMatrices  = extra.BoneMatrices,
                             .Outlined      = extra.Outlined } );
    }

    const Environment SceneRenderer::CreateEnvironment( const Common::Filepath& filepath )
    {
        return {}; // EnvironmentManager::Create( filepath );
    }

    void SceneRenderer::SetEnvironment( const std::shared_ptr<MaterialSkybox>& material )
    {
        UNIQUE_GET_AS( System::SkyboxRenderer, m_RenderSystems["SkyboxSystem"] )->PrepareMaterial( material );
    }

    const std::optional<Environment>& SceneRenderer::GetEnvironment()
    {
        return UNIQUE_GET_AS( System::SkyboxRenderer, m_RenderSystems["SkyboxSystem"] )->GetEnvironment();
    }

    const std::shared_ptr<Desert::Graphic::Image2D> SceneRenderer::GetFinalImage()
    {
        // FXAA writes its own framebuffer downstream of tonemap; otherwise the tonemap output IS final.
        const char* finalSystem =
             ( m_AAMode == Core::AntiAliasingMode::FXAA ) ? "FXAASystem" : "TonemapSystem";

        return std::static_pointer_cast<System::RenderSystem>( m_RenderSystems[finalSystem] )
             ->GetSystemFramebuffer()
             ->GetColorAttachmentImage();
    }

    void SceneRenderer::AddPointLight( ShaderProtocols::PointLightPayload&& pointLight )
    {
        m_PointLight.PointLights.push_back( std::move( pointLight ) );
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

            const auto passFb = pass.CachedRenderPass->GetSpecification().TargetFramebuffer;

            if ( passFb != currentFb )
            {
                if ( currentFb )
                    renderer.EndRenderPass();
                renderer.BeginRenderPass( pass.CachedRenderPass.get(), true );
                currentFb = passFb;
            }

            pass.ExecuteFunc();
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