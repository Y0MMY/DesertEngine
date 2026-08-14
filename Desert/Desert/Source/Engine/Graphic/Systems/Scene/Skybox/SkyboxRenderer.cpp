#include "SkyboxRenderer.hpp"
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Graphic/SkyPayload.hpp>
#include <Common/Core/Logger.hpp>
#include <Common/Core/Profiler.hpp>
#include <Engine/Graphic/FallbackTextures.hpp>

#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::Graphic::System
{
    namespace
    {
        // LUT extents from the paper (and UE's defaults): transmittance 256x64, multi-scattering 32x32.
        // Not authorable — the sizes are part of the parameterisation the shaders and the SkyMedium
        // tests share, and 136 KiB total leaves nothing worth a quality dial.
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
            // pipelines, the same price the clouds pay for five); the IMAGES stay lazy, so a scene on
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

            m_TransmittanceLutPipeline = makeCompute( "SkyTransmittanceLut" );
            m_MultiScatterLutPipeline  = makeCompute( "SkyMultiScatterLut" );
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

    void SkyboxRenderer::DispatchTransmittanceLut()
    {
        auto& renderer = Renderer::GetInstance();

        renderer.ComputeImageBeginWrite( m_TransmittanceLut.get() );
        m_TransmittanceLutPipeline->SetOutput( kSkyTransmittanceLutOutputBinding, m_TransmittanceLut.get(), 0 );
        m_TransmittanceLutPipeline->SetStorageBuffer( kSkyPayloadBinding, m_SkyParams.get() );
        renderer.DispatchComputeInFrame( m_TransmittanceLutPipeline.get(), LutGroupCount( kTransmittanceLutWidth ),
                                         LutGroupCount( kTransmittanceLutHeight ), 1 );
        renderer.ComputeImageEndWrite( m_TransmittanceLut.get() );
    }

    void SkyboxRenderer::DispatchMultiScatterLut()
    {
        auto& renderer = Renderer::GetInstance();

        renderer.ComputeImageBeginWrite( m_MultiScatterLut.get() );
        m_MultiScatterLutPipeline->SetOutput( kSkyMultiScatterLutOutputBinding, m_MultiScatterLut.get(), 0 );
        m_MultiScatterLutPipeline->SetStorageBuffer( kSkyPayloadBinding, m_SkyParams.get() );
        // Written by DispatchTransmittanceLut a moment ago; EndWrite left it sampleable, exactly as the
        // cloud weather map is sampleable by the raymarch that follows it.
        m_MultiScatterLutPipeline->SetInput( kSkyTransmittanceLutBinding, m_TransmittanceLut.get() );
        renderer.DispatchComputeInFrame( m_MultiScatterLutPipeline.get(), LutGroupCount( kMultiScatterLutSize ),
                                         LutGroupCount( kMultiScatterLutSize ), 1 );
        renderer.ComputeImageEndWrite( m_MultiScatterLut.get() );
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
        if ( m_LutsValid && wanted == m_LutBaked )
            return;

        DESERT_PROFILE_SCOPE( "Sky: AtmosphereLuts" );

        // Transmittance strictly first: the multi-scattering march samples it per step.
        DispatchTransmittanceLut();
        DispatchMultiScatterLut();

        m_LutBaked  = wanted;
        m_LutsValid = true;

        LOG_INFO( "[SkyAtmosphere] Atmosphere LUTs dispatched (transmittance {}x{}, multi-scattering "
                  "{}x{}) — the atmosphere parameter fingerprint changed.",
                  kTransmittanceLutWidth, kTransmittanceLutHeight, kMultiScatterLutSize, kMultiScatterLutSize );
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
                                           const SkySettings& sky, const glm::vec3& cloudLuminanceScale )
    {
        m_UseProceduralSky = enabled;
        m_SunDir           = glm::normalize( sunDir );
        m_Sky              = sky;
        m_BakeRequested    = m_BakeRequested || bakeNow;

        // The evaluated sky other renderers consume. It is rebuilt from this frame's numbers rather than
        // accumulated, so a frame in which the sky is switched off publishes Valid == false immediately.
        //
        // The parameter buffer is part of the condition, not an afterthought: the published contract is
        // "ParamsBuffer is null exactly when Valid is false", and a missing ProceduralSky shader leaves the
        // buffer uncreated. Publishing Valid == true with no buffer would hand a consumer a null it was
        // told could not happen.
        if ( enabled && m_SkyParams )
        {
            m_Atmosphere = EvaluateAtmosphere( m_Sky, m_SunDir, m_SkyParams.get() );

            // The sun light's Cloud Scattered Luminance Scale (UE semantics): scales what the CLOUDS
            // receive from this sun, and nothing else — SunIrradiance's only consumer is the cloud
            // march (see AtmosphereEnv), which is what makes this a per-light volumetrics control
            // rather than a second sun intensity.
            m_Atmosphere.SunIrradiance *= cloudLuminanceScale;

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

        Environment baked = EnvironmentManager::CreateProcedural( size.Width, size.Height, m_SkyParams.get() );
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

        // Engine-generated procedural atmosphere (no HDR asset needed).
        if ( m_UseProceduralSky && m_ProceduralPipeline && m_ProceduralMaterial && m_ActiveCamera )
        {
            m_ProceduralMaterial->Update( m_ActiveCamera, m_SkyParams );
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
