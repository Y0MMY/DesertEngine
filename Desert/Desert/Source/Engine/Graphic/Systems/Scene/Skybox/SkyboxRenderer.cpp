#include "SkyboxRenderer.hpp"
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Graphic/SkyGroundTransmittance.hpp>
#include <Engine/Graphic/SkyPayload.hpp>
#include <Common/Core/Logger.hpp>
#include <Common/Core/Profiler.hpp>
#include <Engine/Graphic/FallbackTextures.hpp>

#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::Graphic::System
{
    namespace
    {
        // LUT extents from the paper (and UE's defaults): transmittance 256x64, multi-scattering 32x32,
        // Sky-View 192x104 (kSkyViewLutWidth/Height in SkyPayload.hpp — the shaders mirror that pair).
        // Not authorable — the sizes are part of the parameterisation the shaders and the SkyMedium /
        // SkyScattering tests share, and ~292 KiB total leaves nothing worth a quality dial.
        constexpr uint32_t kTransmittanceLutWidth  = 256;
        constexpr uint32_t kTransmittanceLutHeight = 64;
        constexpr uint32_t kMultiScatterLutSize    = 32;

        constexpr uint32_t kLutWorkGroupSize = 8; // LocalSize(8, 8, 1) in both LUT shaders

        constexpr uint32_t LutGroupCount( uint32_t extent )
        {
            return ( extent + kLutWorkGroupSize - 1 ) / kLutWorkGroupSize;
        }

        double LutBytesToMiB( uint64_t bytes )
        {
            return static_cast<double>( bytes ) / ( 1024.0 * 1024.0 );
        }
    } // namespace

    Common::BoolResultStr SkyboxRenderer::Initialize()
    {
        const auto& compositeFramebuffer = m_TargetFramebuffer.lock();
        if ( !compositeFramebuffer )
        {
            DESERT_VERIFY( false );
        }

        constexpr std::string_view debugName = "Skybox";

        // RenderPass
        RenderPassSpecification rpSpec;
        rpSpec.DebugName         = debugName;
        rpSpec.TargetFramebuffer = compositeFramebuffer;

        // Pipeline
        m_Shader = Runtime::ResourceRegistry::GetShaderService()->GetByName( "Skybox" );

        Graphic::GraphicsPipelineSpecification pipeSpec;
        pipeSpec.DebugName   = debugName;
        pipeSpec.Framebuffer = compositeFramebuffer;
        pipeSpec.Shader      = m_Shader;

        pipeSpec.CullMode          = CullMode::None;
        pipeSpec.DepthTestEnabled  = false;
        pipeSpec.DepthWriteEnabled = false;

        m_Pipeline = Graphic::GraphicsPipeline::Create( pipeSpec );
        m_Pipeline->Invalidate();

        // Procedural sky: same fullscreen-quad pass/target, but the engine-generated atmosphere shader.
        m_ProceduralShader = Runtime::ResourceRegistry::GetShaderService()->GetByName( "ProceduralSky" );
        if ( m_ProceduralShader )
        {
            Graphic::GraphicsPipelineSpecification skySpec;
            skySpec.DebugName         = "ProceduralSky";
            skySpec.Framebuffer       = compositeFramebuffer;
            skySpec.Shader            = m_ProceduralShader;
            skySpec.CullMode          = CullMode::None;
            skySpec.DepthTestEnabled  = false;
            skySpec.DepthWriteEnabled = false;

            m_ProceduralPipeline = Graphic::GraphicsPipeline::Create( skySpec );
            m_ProceduralPipeline->Invalidate();

            m_ProceduralMaterial = std::make_shared<MaterialProceduralSky>();

            // Created here, not through shader reflection: the reflection path allocates a fixed 36 bytes,
            // which is not this block. persistent = false is load-bearing — it gives the backend one copy
            // per (frame in flight x renderer slot), which is what keeps a second live SceneRenderer (the
            // mesh preview, a thumbnail, another scene view) from overwriting this one's sky.
            m_SkyParams = ShaderResources::StorageBuffer::Create( "SkyBuffer", kSkyPayloadBytes,
                                                                  kSkyPayloadBinding, /*persistent=*/false );

            // The physical atmosphere's LUT pipelines. Built up front (they are two small compute
            // pipelines); the IMAGES stay lazy, so a scene on
            // the artistic gradient allocates nothing.
            const auto makeCompute = [shaderService = Runtime::ResourceRegistry::GetShaderService()](
                                          const char* name ) -> std::shared_ptr<ComputePipeline>
            {
                const auto shader = shaderService->GetByName( name );
                if ( !shader )
                {
                    LOG_ERROR( "[SkyAtmosphere] Compute shader '{}' is not registered — expected "
                               "Editor/Resources/Shaders/Programs/Sky/{}.shader. The physical "
                               "atmosphere's LUTs will not be built for this view.",
                               name, name );
                    return nullptr;
                }
                auto pipeline = ComputePipeline::Create( { .Shader = shader, .DebugName = name } );
                if ( !pipeline )
                    return nullptr;
                // Create() allocates the object, Invalidate() builds the Vulkan pipeline — the same
                // two-line idiom as every other compute call site in the engine.
                pipeline->Invalidate();
                return pipeline;
            };

            m_TransmittanceLutPipeline  = makeCompute( "SkyTransmittanceLut" );
            m_MultiScatterLutPipeline   = makeCompute( "SkyMultiScatterLut" );
            m_SkyViewLutPipeline        = makeCompute( "SkyViewLut" );
            m_AerialPerspectivePipeline = makeCompute( "SkyAerialPerspectiveLut" );
            m_DistantLightPipeline      = makeCompute( "SkyDistantLight" );
        }

        return BOOLSUCCESS;
    }

    SkyboxRenderer::AtmosphereLutFingerprint SkyboxRenderer::LutFingerprintOf( const SkySettings& sky )
    {
        return AtmosphereLutFingerprint{ .RayleighScattering        = sky.RayleighScattering,
                                         .RayleighExpDistributionKm = sky.RayleighExpDistributionKm,
                                         .MieScattering             = sky.MieScattering,
                                         .MieAbsorption             = sky.MieAbsorption,
                                         .MieExpDistributionKm      = sky.MieExpDistributionKm,
                                         .OzoneAbsorption           = sky.OzoneAbsorption,
                                         .OzoneTipAltitudeKm        = sky.OzoneTipAltitudeKm,
                                         .OzoneTipValue             = sky.OzoneTipValue,
                                         .OzoneTentWidthKm          = sky.OzoneTentWidthKm,
                                         .GroundAlbedo              = sky.GroundAlbedo,
                                         .AtmosphereHeightKm        = sky.AtmosphereHeightKm,
                                         .MultiScatteringFactor     = sky.MultiScatteringFactor,
                                         .PlanetRadius              = sky.PlanetRadius };
    }

    bool SkyboxRenderer::EnsureAtmosphereLutResources()
    {
        if ( m_LutResourcesFailed )
            return false;
        if ( m_TransmittanceLut && m_MultiScatterLut )
            return true;

        const Core::Formats::Image2DSpecification transmittanceSpec{
             .Tag        = "SkyTransmittanceLut",
             .Width      = kTransmittanceLutWidth,
             .Height     = kTransmittanceLutHeight,
             .Format     = Core::Formats::ImageFormat::RGBA16F,
             .Mips       = 1u,
             .Usage      = Core::Formats::Image2DUsage::Image2D,
             .Properties = Core::Formats::Storage | Core::Formats::Sample,
        };
        m_TransmittanceLut = Image2D::Create( transmittanceSpec, nullptr );

        const Core::Formats::Image2DSpecification multiScatterSpec{
             .Tag        = "SkyMultiScatterLut",
             .Width      = kMultiScatterLutSize,
             .Height     = kMultiScatterLutSize,
             .Format     = Core::Formats::ImageFormat::RGBA16F,
             .Mips       = 1u,
             .Usage      = Core::Formats::Image2DUsage::Image2D,
             .Properties = Core::Formats::Storage | Core::Formats::Sample,
        };
        m_MultiScatterLut = Image2D::Create( multiScatterSpec, nullptr );

        if ( !m_TransmittanceLut || !m_MultiScatterLut )
        {
            LOG_ERROR( "[SkyAtmosphere] The atmosphere LUTs could not be created (transmittance "
                       "{}x{}: {}, multi-scattering {}x{}: {}); the physical atmosphere will not be "
                       "built for this view.",
                       kTransmittanceLutWidth, kTransmittanceLutHeight, m_TransmittanceLut ? "ok" : "FAILED",
                       kMultiScatterLutSize, kMultiScatterLutSize, m_MultiScatterLut ? "ok" : "FAILED" );
            m_TransmittanceLut.reset();
            m_MultiScatterLut.reset();
            m_LutResourcesFailed = true;
            return false;
        }

        LOG_INFO(
             "[SkyAtmosphere] Transmittance LUT {}x{} + multi-scattering LUT {}x{} RGBA16F "
             "({:.2f} MiB) allocated for the physical atmosphere.",
             kTransmittanceLutWidth, kTransmittanceLutHeight, kMultiScatterLutSize, kMultiScatterLutSize,
             LutBytesToMiB( Core::Formats::CalculateImageSize( kTransmittanceLutWidth, kTransmittanceLutHeight,
                                                               Core::Formats::ImageFormat::RGBA16F ) +
                            Core::Formats::CalculateImageSize( kMultiScatterLutSize, kMultiScatterLutSize,
                                                               Core::Formats::ImageFormat::RGBA16F ) ) );
        return true;
    }

    void SkyboxRenderer::DispatchCachedAtmosphereLuts( bool inFrame )
    {
        auto& renderer = Renderer::GetInstance();

        // Transmittance strictly first: the multi-scattering march samples it per step.
        m_TransmittanceLutPipeline->SetOutput( kSkyTransmittanceLutOutputBinding, m_TransmittanceLut.get(), 0 );
        m_TransmittanceLutPipeline->SetStorageBuffer( kSkyPayloadBinding, m_SkyParams.get() );
        if ( inFrame )
        {
            renderer.ComputeImageBeginWrite( m_TransmittanceLut.get() );
            renderer.DispatchComputeInFrame( m_TransmittanceLutPipeline.get(),
                                             LutGroupCount( kTransmittanceLutWidth ),
                                             LutGroupCount( kTransmittanceLutHeight ), 1 );
            renderer.ComputeImageEndWrite( m_TransmittanceLut.get() );
        }
        else
        {
            // Immediate submit (the bake path, which runs OUTSIDE a frame): ComputePipeline::Dispatch
            // owns the output's layout round-trip and leaves it sampleable, the same contract
            // EndWrite provides in-frame.
            m_TransmittanceLutPipeline->Dispatch( LutGroupCount( kTransmittanceLutWidth ),
                                                  LutGroupCount( kTransmittanceLutHeight ), 1 );
        }

        m_MultiScatterLutPipeline->SetOutput( kSkyMultiScatterLutOutputBinding, m_MultiScatterLut.get(), 0 );
        m_MultiScatterLutPipeline->SetStorageBuffer( kSkyPayloadBinding, m_SkyParams.get() );
        // Written a moment ago; both paths leave it sampleable by the dispatch that follows.
        m_MultiScatterLutPipeline->SetInput( kSkyTransmittanceLutBinding, m_TransmittanceLut.get() );
        if ( inFrame )
        {
            renderer.ComputeImageBeginWrite( m_MultiScatterLut.get() );
            renderer.DispatchComputeInFrame( m_MultiScatterLutPipeline.get(),
                                             LutGroupCount( kMultiScatterLutSize ),
                                             LutGroupCount( kMultiScatterLutSize ), 1 );
            renderer.ComputeImageEndWrite( m_MultiScatterLut.get() );
        }
        else
        {
            m_MultiScatterLutPipeline->Dispatch( LutGroupCount( kMultiScatterLutSize ),
                                                 LutGroupCount( kMultiScatterLutSize ), 1 );
        }
    }

    bool SkyboxRenderer::EnsureSkyViewLutResources()
    {
        if ( m_SkyViewResourcesFailed )
            return false;
        if ( m_SkyViewLut )
            return true;

        const Core::Formats::Image2DSpecification skyViewSpec{
             .Tag        = "SkyViewLut",
             .Width      = kSkyViewLutWidth,
             .Height     = kSkyViewLutHeight,
             .Format     = Core::Formats::ImageFormat::RGBA16F,
             .Mips       = 1u,
             .Usage      = Core::Formats::Image2DUsage::Image2D,
             .Properties = Core::Formats::Storage | Core::Formats::Sample,
        };
        m_SkyViewLut = Image2D::Create( skyViewSpec, nullptr );

        if ( !m_SkyViewLut )
        {
            LOG_ERROR( "[SkyAtmosphere] The Sky-View LUT ({}x{} RGBA16F) could not be created; the "
                       "physical sky will not render for this view.",
                       kSkyViewLutWidth, kSkyViewLutHeight );
            m_SkyViewResourcesFailed = true;
            return false;
        }

        LOG_INFO( "[SkyAtmosphere] Sky-View LUT {}x{} RGBA16F ({:.2f} MiB) allocated for the physical "
                  "atmosphere — refilled every frame while the model is active.",
                  kSkyViewLutWidth, kSkyViewLutHeight,
                  LutBytesToMiB( Core::Formats::CalculateImageSize( kSkyViewLutWidth, kSkyViewLutHeight,
                                                                    Core::Formats::ImageFormat::RGBA16F ) ) );
        return true;
    }

    void SkyboxRenderer::DispatchSkyViewLut()
    {
        auto& renderer = Renderer::GetInstance();

        const SkyViewLutPush push{ .CameraPosWorld = glm::vec4( m_ActiveCamera->GetPosition(), 0.0f ) };

        renderer.ComputeImageBeginWrite( m_SkyViewLut.get() );
        m_SkyViewLutPipeline->SetOutput( kSkyViewLutOutputBinding, m_SkyViewLut.get(), 0 );
        m_SkyViewLutPipeline->SetStorageBuffer( kSkyPayloadBinding, m_SkyParams.get() );
        m_SkyViewLutPipeline->SetInput( kSkyTransmittanceLutBinding, m_TransmittanceLut.get() );
        m_SkyViewLutPipeline->SetInput( kSkyMultiScatterLutBinding, m_MultiScatterLut.get() );
        m_SkyViewLutPipeline->SetPushConstants( &push, static_cast<uint32_t>( sizeof( push ) ) );
        renderer.DispatchComputeInFrame( m_SkyViewLutPipeline.get(), LutGroupCount( kSkyViewLutWidth ),
                                         LutGroupCount( kSkyViewLutHeight ), 1 );
        renderer.ComputeImageEndWrite( m_SkyViewLut.get() );
    }

    bool SkyboxRenderer::EnsureAerialPerspectiveResources()
    {
        if ( m_AerialPerspectiveResourcesFailed )
            return false;
        if ( m_AerialPerspectiveLut )
            return true;

        const Core::Formats::Image3DSpecification apSpec{
             .Tag        = "SkyAerialPerspectiveLut",
             .Width      = kAerialPerspectiveWidth,
             .Height     = kAerialPerspectiveHeight,
             .Depth      = kAerialPerspectiveDepth,
             .Format     = Core::Formats::ImageFormat::RGBA16F,
             .Properties = Core::Formats::Storage | Core::Formats::Sample,
        };
        m_AerialPerspectiveLut = Image3D::Create( apSpec );

        if ( !m_AerialPerspectiveLut )
        {
            LOG_ERROR( "[SkyAtmosphere] The camera aerial-perspective volume ({}x{}x{} RGBA16F) could not "
                       "be created; geometry in this view will receive no distance haze.",
                       kAerialPerspectiveWidth, kAerialPerspectiveHeight, kAerialPerspectiveDepth );
            m_AerialPerspectiveResourcesFailed = true;
            return false;
        }

        LOG_INFO( "[SkyAtmosphere] Aerial-perspective volume {}x{}x{} RGBA16F ({:.2f} MiB) allocated for "
                  "the physical atmosphere — refilled every frame while the model is active.",
                  kAerialPerspectiveWidth, kAerialPerspectiveHeight, kAerialPerspectiveDepth,
                  LutBytesToMiB( Core::Formats::CalculateImageSize(
                       kAerialPerspectiveWidth, kAerialPerspectiveHeight, kAerialPerspectiveDepth,
                       Core::Formats::ImageFormat::RGBA16F ) ) );
        return true;
    }

    void SkyboxRenderer::DispatchAerialPerspectiveLut()
    {
        auto& renderer = Renderer::GetInstance();

        SkyAerialPerspectivePush push{};
        push.InverseViewProjection =
             glm::inverse( m_ActiveCamera->GetProjectionMatrix() * m_ActiveCamera->GetViewMatrix() );
        push.CameraPosWorld = glm::vec4( m_ActiveCamera->GetPosition(), 0.0f );
        push.VolumeParams =
             glm::vec4( m_Sky.AerialPerspectiveDistanceKm, m_Sky.AerialPerspectiveStartDepthKm, 0.0f, 0.0f );

        renderer.ComputeImageBeginWrite( m_AerialPerspectiveLut.get() );
        m_AerialPerspectivePipeline->SetOutput( kSkyAerialPerspectiveOutputBinding, m_AerialPerspectiveLut.get(),
                                                0 );
        m_AerialPerspectivePipeline->SetStorageBuffer( kSkyPayloadBinding, m_SkyParams.get() );
        m_AerialPerspectivePipeline->SetInput( kSkyTransmittanceLutBinding, m_TransmittanceLut.get() );
        m_AerialPerspectivePipeline->SetInput( kSkyMultiScatterLutBinding, m_MultiScatterLut.get() );
        m_AerialPerspectivePipeline->SetPushConstants( &push, static_cast<uint32_t>( sizeof( push ) ) );

        // ONE INVOCATION PER FROXEL COLUMN — the z extent is walked inside the shader so consecutive
        // slices share one quadrature, so the dispatch is 2D over the volume's x/y and never over z.
        renderer.DispatchComputeInFrame( m_AerialPerspectivePipeline.get(),
                                         LutGroupCount( kAerialPerspectiveWidth ),
                                         LutGroupCount( kAerialPerspectiveHeight ), 1 );
        renderer.ComputeImageEndWrite( m_AerialPerspectiveLut.get() );
    }

    bool SkyboxRenderer::EnsureDistantLightResources()
    {
        if ( m_DistantLightResourcesFailed )
            return false;
        if ( m_DistantLight )
            return true;

        // ONE TEXEL: (0,0) the full-sphere mean the height fog reads — one march, one reduction, see
        // Programs/Sky/SkyDistantLight.shader. RGBA32F rather than the RGBA16F every other LUT uses: at
        // 16 bytes total the exact format is free, and this value is added to a fog colour at night
        // radiances where a half's three decimal digits would quantise into visible steps as the sun
        // sets.
        const Core::Formats::Image2DSpecification distantSpec{
             .Tag        = "SkyDistantLight",
             .Width      = kDistantLightWidth,
             .Height     = 1,
             .Format     = Core::Formats::ImageFormat::RGBA32F,
             .Mips       = 1u,
             .Usage      = Core::Formats::Image2DUsage::Image2D,
             .Properties = Core::Formats::Storage | Core::Formats::Sample,
        };
        m_DistantLight = Image2D::Create( distantSpec, nullptr );

        if ( !m_DistantLight )
        {
            LOG_ERROR( "[SkyAtmosphere] The distant sky light ({}x1 RGBA32F) could not be created; the "
                       "atmospheric fog in this view will fall back to its authored colour with no sky "
                       "ambient.",
                       kDistantLightWidth );
            m_DistantLightResourcesFailed = true;
            return false;
        }

        LOG_INFO( "[SkyAtmosphere] Distant sky light {}x1 RGBA32F allocated — {} directions marched at "
                  "{} km every frame, reduced to the full-sphere mean the fog reads.",
                  kDistantLightWidth, 64, 6 );
        return true;
    }

    void SkyboxRenderer::DispatchDistantLight()
    {
        auto& renderer = Renderer::GetInstance();

        renderer.ComputeImageBeginWrite( m_DistantLight.get() );
        m_DistantLightPipeline->SetOutput( kSkyDistantLightOutputBinding, m_DistantLight.get(), 0 );
        m_DistantLightPipeline->SetStorageBuffer( kSkyPayloadBinding, m_SkyParams.get() );
        m_DistantLightPipeline->SetInput( kSkyTransmittanceLutBinding, m_TransmittanceLut.get() );
        m_DistantLightPipeline->SetInput( kSkyMultiScatterLutBinding, m_MultiScatterLut.get() );

        // ONE WORKGROUP, and it must stay one: the 64 directions are reduced in groupshared memory,
        // which no second group can see. The shader's LocalSize is 64 for the same reason.
        renderer.DispatchComputeInFrame( m_DistantLightPipeline.get(), 1, 1, 1 );
        renderer.ComputeImageEndWrite( m_DistantLight.get() );
    }

    void SkyboxRenderer::ExecuteAtmosphereLuts()
    {
        // The gradient model never reaches past this line: no allocation, no dispatch, no fingerprint —
        // an existing scene pays literally nothing for the physical atmosphere's machinery.
        if ( !m_UseProceduralSky || m_Sky.Model != ECS::SkyModel::PhysicalAtmosphere )
            return;

        // A missing shader was already reported by Initialize with the path it expected.
        if ( !m_TransmittanceLutPipeline || !m_MultiScatterLutPipeline || !m_SkyParams )
            return;

        if ( !EnsureAtmosphereLutResources() )
            return;

        const AtmosphereLutFingerprint wanted = LutFingerprintOf( m_Sky );
        if ( !m_LutsValid || wanted != m_LutBaked )
        {
            DESERT_PROFILE_SCOPE( "Sky: AtmosphereLuts" );

            DispatchCachedAtmosphereLuts( /*inFrame=*/true );

            m_LutBaked  = wanted;
            m_LutsValid = true;

            LOG_INFO( "[SkyAtmosphere] Atmosphere LUTs dispatched (transmittance {}x{}, multi-scattering "
                      "{}x{}) — the atmosphere parameter fingerprint changed.",
                      kTransmittanceLutWidth, kTransmittanceLutHeight, kMultiScatterLutSize,
                      kMultiScatterLutSize );
        }

        // The Sky-View LUT, every frame: it depends on the camera's altitude and the sun, which the
        // cached pair deliberately does not. The sky pass samples the previous frame's fill (this slot
        // runs after the graph recorded the Sky pass) — invisible at 192x104 of slowly-varying sky.
        if ( m_SkyViewLutPipeline && m_ActiveCamera && EnsureSkyViewLutResources() )
        {
            DESERT_PROFILE_SCOPE( "Sky: SkyViewLut" );
            DispatchSkyViewLut();
        }

        // The camera aerial-perspective volume, every frame and for the same reason — it is the camera's
        // own frustum. Published on the AtmosphereEnv only once the fill has actually happened: the
        // handle being non-null is the contract that says "there is aerial perspective this frame", and
        // the atmospheric-fog pass (dispatched immediately after this slot) composes the identity when
        // it is null rather than sampling a volume nobody wrote.
        if ( m_AerialPerspectivePipeline && m_ActiveCamera && EnsureAerialPerspectiveResources() )
        {
            DESERT_PROFILE_SCOPE( "Sky: AerialPerspectiveLut" );
            DispatchAerialPerspectiveLut();

            m_Atmosphere.AerialPerspectiveVolume            = m_AerialPerspectiveLut.get();
            m_Atmosphere.AerialPerspectiveDepthKm           = m_Sky.AerialPerspectiveDistanceKm;
            m_Atmosphere.AerialPerspectiveViewDistanceScale = m_Sky.AerialPerspectiveViewDistanceScale;
        }

        // The distant sky light, every frame: it is a function of the sun, which moves. Published on
        // the AtmosphereEnv only once the fill has happened, on the same contract as the volume above —
        // a non-null handle IS the statement "there is a physical average sky this frame", and the
        // atmospheric-fog pass keeps its authored colour when it is null rather than sampling a texel
        // nobody wrote.
        if ( m_DistantLightPipeline && EnsureDistantLightResources() )
        {
            DESERT_PROFILE_SCOPE( "Sky: DistantSkyLight" );
            DispatchDistantLight();

            m_Atmosphere.DistantSkyLight = m_DistantLight.get();
        }
    }

    bool SkyboxRenderer::EnsureCachedLutsForBake()
    {
        if ( !m_TransmittanceLutPipeline || !m_MultiScatterLutPipeline || !m_SkyParams )
            return false;
        if ( !EnsureAtmosphereLutResources() )
            return false;

        const AtmosphereLutFingerprint wanted = LutFingerprintOf( m_Sky );
        if ( m_LutsValid && wanted == m_LutBaked )
            return true;

        // First physical bake of this renderer (or an atmosphere edit in the same frame): the in-frame
        // slot has not run yet, and the bake cannot march empty LUTs. Immediate dispatches fill them
        // now; the caller idles the device around the bake anyway.
        DispatchCachedAtmosphereLuts( /*inFrame=*/false );
        m_LutBaked  = wanted;
        m_LutsValid = true;

        LOG_INFO( "[SkyAtmosphere] Atmosphere LUTs dispatched immediately for the environment bake — "
                  "the bake ran before this frame's in-frame LUT slot." );
        return true;
    }

    void SkyboxRenderer::PrepareCamera( Core::Camera* camera )
    {
        m_ActiveCamera = camera;
    }

    void SkyboxRenderer::PrepareMaterial( const std::shared_ptr<MaterialSkybox>& material, float intensity )
    {
        // Only record the material here. This can run from the skybox-load command (ExecuteAll) BEFORE
        // BeginScene/PrepareCamera, so the active camera may not exist yet — the camera-dependent bind
        // is deferred to Render(), which always runs with a valid camera.
        if ( !material )
        {
            return;
        }

        m_MaterialSkybox  = material;
        m_SkyboxIntensity = intensity;
    }

    void SkyboxRenderer::SetProceduralSky( bool enabled, const glm::vec3& sunDir, bool bakeNow,
                                           const SkySettings& sky, const SunLightFx& fx )
    {
        m_UseProceduralSky = enabled;
        m_SunDir           = glm::normalize( sunDir );
        m_Sky              = sky;
        m_BakeRequested    = m_BakeRequested || bakeNow;

        // The evaluated sky other renderers consume. It is rebuilt from this frame's numbers rather than
        // accumulated, so a frame in which the sky is switched off publishes Valid == false immediately.
        //
        // The parameter buffer is part of the condition, not an afterthought: a missing ProceduralSky
        // shader leaves the buffer uncreated, and with no buffer the sky is not being evaluated on the GPU
        // at all. Publishing Valid == true then would hand a consumer a sun no pass agrees with.
        if ( enabled && m_SkyParams )
        {
            m_Atmosphere = EvaluateAtmosphere( m_Sky, m_SunDir );

            // UE's PrepareSunLightProxy, scoped to the model by the teamlead's decision (research doc
            // section 5, Q3): in PhysicalAtmosphere the sun light's colour is multiplied by the
            // atmosphere's transmittance toward the sun at ground level; in ArtisticGradient the
            // coupling does not exist, and the documented independence of sky radiance and surface
            // illuminance stands. The per-light opt-out is the second gate.
            //
            // Evaluated HERE rather than by the consumer because this is where the sun and the medium
            // are both in hand, and because it must be one value per frame: two consumers each
            // marching it would be the same quantity computed twice.
            const bool couple =
                 m_Sky.Model == ECS::SkyModel::PhysicalAtmosphere && fx.AffectedByAtmosphereTransmittance;
            m_Atmosphere.SunTransmittanceAtGround =
                 couple ? SunTransmittanceAtGround( m_Sky, m_Atmosphere.SunDirection ) : glm::vec3( 1.0f );

            // The same product SceneRenderer::OnUpdate forms for the light's own colour, published once
            // so the fog's directional lobe reads the sun on the ground instead of re-deriving it from a
            // light list it has no business walking (UE publishes it on the View UB for the same reason).
            m_Atmosphere.SunIlluminanceOnGround = fx.OuterSpaceIlluminance * m_Atmosphere.SunTransmittanceAtGround;

            // A sun below the horizon is a legal authored state (it is night), but it is also what an
            // inverted Translation looks like — and that mistake shipped in four scenes. Say it once, with
            // the number, rather than leaving "why is everything black" to be discovered.
            if ( m_Atmosphere.SunDirection.y < 0.0f && !m_BelowHorizonLogged )
            {
                LOG_WARN( "[SkyAtmosphere] The atmosphere sun is BELOW the horizon (elevation {:.1f} deg) — "
                          "the sky renders as night. If that is not intended, the light's Translation is "
                          "the direction the light TRAVELS, so a sun overhead points DOWN.",
                          glm::degrees( std::asin( glm::clamp( m_Atmosphere.SunDirection.y, -1.0f, 1.0f ) ) ) );
                m_BelowHorizonLogged = true;
            }
        }
        else
        {
            m_Atmosphere = AtmosphereEnv{};
        }

        UploadSkyParams();
    }

    void SkyboxRenderer::UploadSkyParams()
    {
        if ( !m_SkyParams || !m_UseProceduralSky )
            return;

        const SkyGpuPayload payload = PackSky( m_SunDir, m_Sky );
        m_SkyParams->SetData( &payload, kSkyPayloadBytes );
    }

    void SkyboxRenderer::EnsureProceduralEnvironment( float deltaSeconds )
    {
        if ( !m_UseProceduralSky )
            return;

        const bool explicitRequest = m_BakeRequested;
        m_BakeRequested            = false;

        // How long the sun has held still. Compared by direction rather than by "did anything write it",
        // because the time-of-day driver rewrites the same value every frame when it is paused.
        const float dt = glm::max( deltaSeconds, 0.0f );
        if ( glm::dot( m_LastSeenSunDir - m_SunDir, m_LastSeenSunDir - m_SunDir ) > 1e-12f )
        {
            m_LastSeenSunDir       = m_SunDir;
            m_SecondsSinceSunMoved = 0.0f;
        }
        else
        {
            m_SecondsSinceSunMoved += dt;
        }

        if ( !ShouldRebakeSkyEnvironment( m_BakedSunDir, m_SunDir, m_Sky.RebakeSunAngleThreshold,
                                          m_Sky.AutoRebakeEnvironment, static_cast<bool>( m_ProceduralEnv ),
                                          explicitRequest ) )
        {
            m_SecondsSinceStale = 0.0f;
            return;
        }

        m_SecondsSinceStale += dt;

        // The Bake button and the very first bake are answers to a question the user just asked, or the
        // difference between an ambient-lit world and an unlit one. Neither waits.
        const bool immediate = explicitRequest || !m_ProceduralEnv;
        if ( !immediate && !SkyEnvironmentRebakeMayRun( m_SecondsSinceSunMoved, m_SecondsSinceStale,
                                                        kSkyRebakeSettleSeconds, kSkyRebakeMaxDeferSeconds ) )
            return;

        m_SecondsSinceStale = 0.0f;

        const SkyEnvironmentSize size = EnvironmentPanoramaSize( m_Sky.EnvironmentResolution );

        if ( m_Sky.EnvironmentResolution == ECS::SkyEnvironmentResolution::High && !m_HighResCostLogged )
        {
            const SkyEnvironmentCost cost = SkyEnvironmentBakeCost( m_Sky.EnvironmentResolution );
            LOG_INFO( "[SkyAtmosphere] Environment bake at High ({}x{}): panorama {:.1f} MiB + "
                      "radiance/irradiance/prefiltered cubes {:.1f} MiB = {:.1f} MiB — paid PER LIVE "
                      "SceneRenderer ({} live now).",
                      size.Width, size.Height, BytesToMiB( cost.PanoramaBytes ), BytesToMiB( cost.CubeBytes ),
                      BytesToMiB( cost.TotalBytes ), SceneRenderer::GetLiveRendererCount() );
            m_HighResCostLogged = true;
        }

        // The bake runs immediate compute dispatches; idle the device first (mirrors the editor's
        // skybox-swap path) since we're recreating GPU images that prior frames may have referenced.
        Renderer::GetInstance().WaitDeviceIdle();

        // The physical model's bake marches the cached LUTs, which the in-frame slot may not have
        // filled yet (the very first frame bakes before it runs). Guarantee them here; if they cannot
        // exist, the bake would produce a black environment and silently look "done" — skip and say so.
        const bool physical = m_Sky.Model == ECS::SkyModel::PhysicalAtmosphere;
        if ( physical && !EnsureCachedLutsForBake() )
        {
            LOG_ERROR( "[SkyAtmosphere] Environment bake skipped: the physical model's atmosphere LUTs "
                       "are unavailable (see the errors above). The previous environment is kept." );
            return;
        }

        Environment baked = EnvironmentManager::CreateProcedural( size.Width, size.Height, m_SkyParams.get(),
                                                                  physical ? m_TransmittanceLut.get() : nullptr,
                                                                  physical ? m_MultiScatterLut.get() : nullptr );
        if ( !baked )
        {
            // Keep the previous environment and say why; the user can retry with the Bake button. Do NOT
            // stamp m_BakedSunDir — a failed bake must not look like an up-to-date one.
            LOG_ERROR( "[SkyAtmosphere] Environment bake at {}x{} failed — the previous environment is "
                       "kept. The BakeProceduralSky compute shader is the usual cause.",
                       size.Width, size.Height );
            return;
        }

        const Environment previous = m_ProceduralEnv;
        m_ProceduralEnv            = baked;

        // Release the previous baked cubes (the image service owns them until unregistered).
        if ( previous )
        {
            auto* imageService = Runtime::ResourceRegistry::GetImageService();
            imageService->Unregister( previous.RadianceMap );
            imageService->Unregister( previous.IrradianceMap );
            imageService->Unregister( previous.PreFilteredMap );
        }

        m_BakedSunDir = m_SunDir;
    }

    void SkyboxRenderer::RegisterPasses( RenderGraphBuilder& builder )
    {
        auto targetFb = m_TargetFramebuffer.lock();
        if ( !targetFb )
            return;

        builder.AddPass( "SkyboxPass", RenderPhase::Sky, [this]() { Render(); },
                         m_Pipeline ? m_Pipeline->GetSpecification() : GraphicsPipelineSpecification{}, targetFb );
    }

    void SkyboxRenderer::Render()
    {
        auto& renderer = Renderer::GetInstance();

        // Engine-generated procedural atmosphere (no HDR asset needed). The LUTs ride along only once
        // the physical model has allocated them; on the gradient they stay null and the material keeps
        // its fallback descriptors for the two samplers the shader declares.
        if ( m_UseProceduralSky && m_ProceduralPipeline && m_ProceduralMaterial && m_ActiveCamera )
        {
            m_ProceduralMaterial->Update( m_ActiveCamera, m_SkyParams, m_TransmittanceLut.get(),
                                          m_SkyViewLut.get() );
            renderer.SubmitFullscreenQuad( m_ProceduralPipeline.get(),
                                           m_ProceduralMaterial->GetMaterialExecutor() );
            return;
        }

        if ( const auto& material = m_MaterialSkybox.lock() )
        {
            if ( m_ActiveCamera )
                material->Bind( { m_ActiveCamera, m_SkyboxIntensity } );
            renderer.SubmitFullscreenQuad( m_Pipeline.get(), material->GetMaterialExecutor() );
        }
    }

} // namespace Desert::Graphic::System
