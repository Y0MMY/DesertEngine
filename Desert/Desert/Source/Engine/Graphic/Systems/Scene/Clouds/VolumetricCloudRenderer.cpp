#include "VolumetricCloudRenderer.hpp"

#include <Engine/Graphic/Clouds/CloudNoiseRules.hpp>
#include <Engine/Graphic/Clouds/CloudNoiseVolumes.hpp>
#include <Engine/Graphic/RenderGraphSort.hpp>
#include <Engine/Graphic/SkyPayload.hpp>
#include <Engine/Graphic/RenderPhase.hpp>
#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Core/Camera.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <Common/Core/Logger.hpp>
#include <Common/Core/Profiler.hpp>

namespace Desert::Graphic::System
{
    namespace
    {
        // The weather map's edge. Fixed, like the noise volumes' dimensions: it is a memory/quality
        // trade-off made once, and a slider on it would change the meaning of Weather Tile Size.
        constexpr uint32_t kWeatherMapSize = 512;

        // 8x8 in both compute shaders. 64 invocations is inside every implementation's guaranteed
        // maximum, and both dispatches bounds-check, so a target that is not a multiple of 8 is fine.
        constexpr uint32_t kWorkGroupSize = 8;

        // Length of the per-frame jitter sequence. Any small EVEN number does: the dither only has to
        // differ from frame to frame for the temporal stage to average it away, and the checkerboard is
        // phased by the index's parity, which an odd wrap would break once per cycle.
        constexpr uint32_t kJitterSequenceLength = 64;
        static_assert( kJitterSequenceLength % 2 == 0 );

        // The shadow map's resolution. 512 over the default 30 km extent is ~117 m a texel, which is
        // about what the cone march it replaces resolved anyway (its own radius was 400 m).
        constexpr uint32_t kCloudShadowMapSize = 512;

        constexpr const char* kWeatherShaderName   = "CloudWeather";
        constexpr const char* kShadowShaderName    = "CloudShadowMap";
        constexpr const char* kRaymarchShaderName  = "CloudRaymarch";
        constexpr const char* kTemporalShaderName  = "CloudTemporalResolve";
        constexpr const char* kCompositeShaderName = "CloudComposite";

        constexpr uint32_t GroupCount( uint32_t extent )
        {
            return ( extent + kWorkGroupSize - 1 ) / kWorkGroupSize;
        }

        double BytesToMiB( uint64_t bytes )
        {
            return static_cast<double>( bytes ) / ( 1024.0 * 1024.0 );
        }
    } // namespace

    VolumetricCloudRenderer::~VolumetricCloudRenderer() = default;

    VolumetricCloudRenderer::WeatherFingerprint
    VolumetricCloudRenderer::FingerprintOf( const ECS::VolumetricCloudData& data )
    {
        return WeatherFingerprint{ .Coverage          = data.Coverage,
                                   .CoverageContrast  = data.CoverageContrast,
                                   .WarpStrength      = data.WeatherWarpStrength,
                                   .CloudType         = data.CloudType,
                                   .CloudTypeVariance = data.CloudTypeVariance,
                                   .Wetness           = data.Wetness,
                                   .Seed              = data.WeatherSeed,
                                   .Octaves           = data.WeatherOctaves };
    }

    Common::BoolResultStr VolumetricCloudRenderer::Initialize()
    {
        if ( !CreatePipelines() )
            return Common::MakeError( "VolumetricCloudRenderer: the cloud shaders could not be resolved "
                                      "(CloudWeather / CloudRaymarch / CloudComposite)" );

        // Non-persistent, so the driver keeps one copy per (frame x recording renderer slot). A
        // persistent buffer is shared across renderers by design, and the Details mesh preview would
        // then overwrite the viewport's cloud parameters every time it recorded a frame — the exact
        // failure Docs/RENDERER_FRAME_STATE.md documents.
        m_ParamsBuffer = ShaderResources::StorageBuffer::Create( "CloudParams", kCloudPayloadBytes,
                                                                 kCloudParamsBinding, /*persistent=*/false );
        if ( !m_ParamsBuffer )
            return Common::MakeError( "VolumetricCloudRenderer: could not create the cloud parameter buffer" );

        m_CompositeMaterial = std::make_unique<MaterialVolumetricClouds>();
        return BOOLSUCCESS;
    }

    void VolumetricCloudRenderer::Shutdown()
    {
        m_WeatherPipeline.reset();
        m_ShadowPipeline.reset();
        m_RaymarchPipeline.reset();
        m_TemporalPipeline.reset();
        m_CompositePipeline.reset();
        m_CompositeMaterial.reset();
        m_WeatherMap.reset();
        m_ProfileMap.reset();
        m_ProfileLut.reset();
        m_CloudShadowMap.reset();
        m_ScatterImage.reset();
        m_DepthGuideImage.reset();
        m_HistoryImages[0].reset();
        m_HistoryImages[1].reset();
        m_HistoryFilled = false;
        m_ParamsBuffer.reset();
    }

    bool VolumetricCloudRenderer::CreatePipelines()
    {
        const auto shaderService = Runtime::ResourceRegistry::GetShaderService();
        if ( !shaderService )
            return false;

        const auto makeCompute = [&shaderService]( const char* name ) -> std::shared_ptr<ComputePipeline>
        {
            const auto shader = shaderService->GetByName( name );
            if ( !shader )
            {
                LOG_ERROR( "[Clouds] Compute shader '{}' is not registered. Expected "
                           "Editor/Resources/Shaders/Programs/Clouds/{}.shader.",
                           name, name );
                return nullptr;
            }
            auto pipeline = ComputePipeline::Create( { .Shader = shader, .DebugName = name } );
            if ( !pipeline )
                return nullptr;
            // Create() allocates the object, Invalidate() builds the Vulkan pipeline. Both are needed;
            // this is the same two-line idiom as every other compute call site in the engine.
            pipeline->Invalidate();
            return pipeline;
        };

        m_WeatherPipeline  = makeCompute( kWeatherShaderName );
        m_ShadowPipeline   = makeCompute( kShadowShaderName );
        m_RaymarchPipeline = makeCompute( kRaymarchShaderName );
        m_TemporalPipeline = makeCompute( kTemporalShaderName );

        const auto target = m_TargetFramebuffer.lock();
        if ( !target )
            return false;

        const auto compositeShader = shaderService->GetByName( kCompositeShaderName );
        if ( !compositeShader )
        {
            LOG_ERROR( "[Clouds] Graphics shader '{}' is not registered.", kCompositeShaderName );
            return false;
        }

        GraphicsPipelineSpecification spec;
        spec.DebugName   = kCompositeShaderName;
        spec.Shader      = compositeShader;
        spec.Framebuffer = target;

        // Depth state is stated, never inherited. The phase forces nothing, and a fullscreen quad has no
        // meaningful depth of its own — occlusion by scene geometry was already resolved inside the
        // march, which clamped every ray to the distance the depth attachment reported.
        spec.DepthTestEnabled  = false;
        spec.DepthWriteEnabled = false;
        spec.CullMode          = CullMode::None;
        spec.Topology          = PrimitiveTopology::Triangles;

        // scene = cloud.rgb * One + scene * cloud.a. The raymarch emits PREMULTIPLIED radiance with
        // transmittance in alpha, so this is the over-operator with nothing left to reconstruct.
        spec.BlendEnable         = true;
        spec.SrcColorBlendFactor = BlendFactor::One;
        spec.DstColorBlendFactor = BlendFactor::SrcAlpha;

        // The pass is replayed by ExecuteTransparency with a LOAD begin, so the pipeline is built
        // against the framebuffer's LOAD render pass.
        spec.UseLoadRenderPass = true;

        m_CompositePipeline = GraphicsPipeline::Create( spec );
        if ( m_CompositePipeline )
            m_CompositePipeline->Invalidate();

        return m_WeatherPipeline && m_RaymarchPipeline && m_TemporalPipeline && m_CompositePipeline;
    }

    void VolumetricCloudRenderer::SetCloudSettings( bool present, const ECS::VolumetricCloudData& data )
    {
        m_Present = present;
        m_Data    = data;
    }

    bool VolumetricCloudRenderer::EnsureResources( uint32_t width, uint32_t height )
    {
        if ( m_ResourcesFailed )
            return false;

        if ( !m_WeatherMap )
        {
            const Core::Formats::Image2DSpecification spec{
                 .Tag        = "CloudWeatherMap",
                 .Width      = kWeatherMapSize,
                 .Height     = kWeatherMapSize,
                 .Format     = Core::Formats::ImageFormat::RGBA8F,
                 .Mips       = 1u,
                 .Usage      = Core::Formats::Image2DUsage::Image2D,
                 .Properties = Core::Formats::Storage | Core::Formats::Sample,
            };
            m_WeatherMap = Image2D::Create( spec, nullptr );
            if ( !m_WeatherMap )
            {
                LOG_ERROR( "[Clouds] The {}x{} weather map ({:.2f} MiB) could not be created; the cloud "
                           "layer will not render for this view.",
                           kWeatherMapSize, kWeatherMapSize,
                           BytesToMiB( Core::Formats::CalculateImageSize( kWeatherMapSize, kWeatherMapSize,
                                                                          Core::Formats::ImageFormat::RGBA8F ) ) );
                m_ResourcesFailed = true;
                return false;
            }
            m_WeatherValid = false;
        }

        // The second weather image: the per-cell Min/Max Height NDFs. Same size, same tiling and same
        // dispatch as the weather map — see the header of Programs/Clouds/CloudWeather.shader for why
        // one pass writes both.
        if ( !m_ProfileMap )
        {
            const Core::Formats::Image2DSpecification spec{
                 .Tag        = "CloudProfileMap",
                 .Width      = kWeatherMapSize,
                 .Height     = kWeatherMapSize,
                 .Format     = Core::Formats::ImageFormat::RGBA8F,
                 .Mips       = 1u,
                 .Usage      = Core::Formats::Image2DUsage::Image2D,
                 .Properties = Core::Formats::Storage | Core::Formats::Sample,
            };
            m_ProfileMap = Image2D::Create( spec, nullptr );
            if ( !m_ProfileMap )
            {
                LOG_ERROR( "[Clouds] The {}x{} profile map ({:.2f} MiB) could not be created; the cloud "
                           "layer will not render for this view.",
                           kWeatherMapSize, kWeatherMapSize,
                           BytesToMiB( Core::Formats::CalculateImageSize( kWeatherMapSize, kWeatherMapSize,
                                                                          Core::Formats::ImageFormat::RGBA8F ) ) );
                m_ResourcesFailed = true;
                return false;
            }
            m_WeatherValid = false;
        }

        if ( !EnsureProfileLut() )
            return false;

        if ( !m_CloudShadowMap )
        {
            const Core::Formats::Image2DSpecification spec{
                 .Tag        = "CloudShadowMap",
                 .Width      = kCloudShadowMapSize,
                 .Height     = kCloudShadowMapSize,
                 .Format     = Core::Formats::ImageFormat::RGBA16F,
                 .Mips       = 1u,
                 .Usage      = Core::Formats::Image2DUsage::Image2D,
                 .Properties = Core::Formats::Storage | Core::Formats::Sample,
            };
            m_CloudShadowMap = Image2D::Create( spec, nullptr );
            if ( !m_CloudShadowMap )
            {
                // Not fatal: the march falls back to the cone for every sample, which is what it did
                // before this map existed. Say so once rather than rendering slowly and silently.
                LOG_WARN( "[Clouds] The {}x{} cloud shadow map could not be created; the raymarch will "
                          "cone-march every shaded sample instead.",
                          kCloudShadowMapSize, kCloudShadowMapSize );
            }
        }

        const uint32_t scatterW = CloudScaledExtent( width, m_Data.ResolutionScale );
        const uint32_t scatterH = CloudScaledExtent( height, m_Data.ResolutionScale );

        if ( m_ScatterImage && m_DepthGuideImage && scatterW == m_ScatterWidth && scatterH == m_ScatterHeight &&
             m_Data.ResolutionScale == m_ScatterScale )
            return true;

        // The old image may still be referenced by descriptors of frames in flight. This is the same
        // reason the sky bake idles before swapping its cubes.
        if ( m_ScatterImage )
            Renderer::GetInstance().WaitDeviceIdle();

        // The history is sized to the scatter target, so a resize invalidates it as well. Released here
        // rather than resized in place: the accumulated frames were resolved at the old size and mean
        // nothing at the new one, and EnsureHistory rebuilds the pair on the next resolve.
        ReleaseHistory();

        const Core::Formats::Image2DSpecification spec{
             .Tag        = "CloudScatterTransmittance",
             .Width      = scatterW,
             .Height     = scatterH,
             .Format     = Core::Formats::ImageFormat::RGBA16F,
             .Mips       = 1u,
             .Usage      = Core::Formats::Image2DUsage::Image2D,
             .Properties = Core::Formats::Storage | Core::Formats::Sample,
        };

        m_ScatterImage = Image2D::Create( spec, nullptr );
        if ( !m_ScatterImage )
        {
            LOG_ERROR( "[Clouds] The {}x{} RGBA16F scatter target could not be created; the cloud layer "
                       "will not render for this view.",
                       scatterW, scatterH );
            m_ResourcesFailed = true;
            return false;
        }

        // The composite's bilateral guide. RGBA8 and not a float format because it carries ONE number per
        // texel, packed into two normalized channels (Common/CloudTemporal.glslh) — a quarter of the bytes
        // an RGBA16F would spend to say the same thing.
        const Core::Formats::Image2DSpecification guideSpec{
             .Tag        = "CloudDepthGuide",
             .Width      = scatterW,
             .Height     = scatterH,
             .Format     = Core::Formats::ImageFormat::RGBA8F,
             .Mips       = 1u,
             .Usage      = Core::Formats::Image2DUsage::Image2D,
             .Properties = Core::Formats::Storage | Core::Formats::Sample,
        };

        m_DepthGuideImage = Image2D::Create( guideSpec, nullptr );
        if ( !m_DepthGuideImage )
        {
            LOG_ERROR( "[Clouds] The {}x{} RGBA8 cloud depth guide could not be created; the cloud layer "
                       "will not render for this view.",
                       scatterW, scatterH );
            m_ResourcesFailed = true;
            return false;
        }

        m_ScatterWidth   = scatterW;
        m_ScatterHeight  = scatterH;
        m_ScatterScale   = m_Data.ResolutionScale;
        m_HasFrameResult = false;

        // The cost is announced, not discovered in a memory graph later. One line per allocation, and
        // an allocation only happens on a resolution or tier change. The total is the same arithmetic
        // CloudScaledImageBytes states and the unit test pins, so the log and the test cannot drift.
        LOG_INFO( "[Clouds] Scatter target {}x{} RGBA16F ({:.2f} MiB) + depth guide RGBA8 ({:.2f} MiB) + "
                  "weather map {}x{} RGBA8 ({:.2f} MiB) for a {}x{} view. Scaled imagery totals "
                  "{:.2f} MiB with the temporal history and {:.2f} MiB without it.",
                  scatterW, scatterH,
                  BytesToMiB( Core::Formats::CalculateImageSize( scatterW, scatterH,
                                                                 Core::Formats::ImageFormat::RGBA16F ) ),
                  BytesToMiB( Core::Formats::CalculateImageSize( scatterW, scatterH,
                                                                 Core::Formats::ImageFormat::RGBA8F ) ),
                  kWeatherMapSize, kWeatherMapSize,
                  BytesToMiB( Core::Formats::CalculateImageSize( kWeatherMapSize, kWeatherMapSize,
                                                                 Core::Formats::ImageFormat::RGBA8F ) ),
                  width, height,
                  BytesToMiB( CloudScaledImageBytes( width, height, m_Data.ResolutionScale,
                                                     ECS::CloudTemporalMode::Reprojection ) ),
                  BytesToMiB( CloudScaledImageBytes( width, height, m_Data.ResolutionScale,
                                                     ECS::CloudTemporalMode::Off ) ) );

        return true;
    }

    void VolumetricCloudRenderer::ReleaseHistory()
    {
        if ( !m_HistoryImages[0] && !m_HistoryImages[1] )
        {
            m_HistoryFilled = false;
            return;
        }

        // Same rule as the scatter target: descriptors of frames still in flight may name these images.
        Renderer::GetInstance().WaitDeviceIdle();
        m_HistoryImages[0].reset();
        m_HistoryImages[1].reset();
        m_HistoryFilled = false;

        LOG_INFO(
             "[Clouds] Temporal history {}x{} RGBA16F x2 released, freeing {:.2f} MiB.", m_ScatterWidth,
             m_ScatterHeight,
             BytesToMiB( 2u * static_cast<uint64_t>( Core::Formats::CalculateImageSize(
                                   m_ScatterWidth, m_ScatterHeight, Core::Formats::ImageFormat::RGBA16F ) ) ) );
    }

    bool VolumetricCloudRenderer::EnsureHistory()
    {
        if ( m_HistoryFailed )
            return false;
        if ( m_HistoryImages[0] && m_HistoryImages[1] )
            return true;

        for ( uint32_t i = 0; i < 2; ++i )
        {
            const Core::Formats::Image2DSpecification spec{
                 .Tag        = i == 0 ? "CloudHistory0" : "CloudHistory1",
                 .Width      = m_ScatterWidth,
                 .Height     = m_ScatterHeight,
                 .Format     = Core::Formats::ImageFormat::RGBA16F,
                 .Mips       = 1u,
                 .Usage      = Core::Formats::Image2DUsage::Image2D,
                 .Properties = Core::Formats::Storage | Core::Formats::Sample,
            };

            m_HistoryImages[i] = Image2D::Create( spec, nullptr );
            if ( !m_HistoryImages[i] )
            {
                // Latched, never retried per frame, and NOT fatal: without a history the composite reads
                // the marched image directly, which is precisely the Temporal Mode = Off configuration —
                // a noisier sky, not a missing one. Said once, with the number, so the log explains the
                // noise instead of leaving it to be re-diagnosed from a screenshot.
                LOG_ERROR( "[Clouds] The {}x{} RGBA16F temporal history ({:.2f} MiB for the pair) could "
                           "not be created; the cloud layer falls back to the un-accumulated march.",
                           m_ScatterWidth, m_ScatterHeight,
                           BytesToMiB( 2u * static_cast<uint64_t>( Core::Formats::CalculateImageSize(
                                                 m_ScatterWidth, m_ScatterHeight,
                                                 Core::Formats::ImageFormat::RGBA16F ) ) ) );
                m_HistoryImages[0].reset();
                m_HistoryImages[1].reset();
                m_HistoryFailed = true;
                return false;
            }
        }

        m_HistoryWrite  = 0;
        m_HistoryFilled = false;

        LOG_INFO(
             "[Clouds] Temporal history {}x{} RGBA16F x2 ({:.2f} MiB) allocated.", m_ScatterWidth, m_ScatterHeight,
             BytesToMiB( 2u * static_cast<uint64_t>( Core::Formats::CalculateImageSize(
                                   m_ScatterWidth, m_ScatterHeight, Core::Formats::ImageFormat::RGBA16F ) ) ) );
        return true;
    }

    VolumetricCloudRenderer::ProfileFingerprint
    VolumetricCloudRenderer::ProfileFingerprintOf( const ECS::VolumetricCloudData& data )
    {
        return ProfileFingerprint{ .Stratus       = data.StratusGradient,
                                   .Shelf         = data.ShelfGradient,
                                   .Stratocumulus = data.StratocumulusGradient,
                                   .Cumulus       = data.CumulusGradient,
                                   .Congestus     = data.CongestusGradient,
                                   .Anvil         = data.AnvilGradient,
                                   .ShelfForm     = data.ShelfProfileForm,
                                   .CongestusForm = data.CongestusProfileForm,
                                   .AnvilForm     = data.AnvilProfileForm };
    }

    bool VolumetricCloudRenderer::EnsureProfileLut()
    {
        const ProfileFingerprint wanted = ProfileFingerprintOf( m_Data );
        if ( m_ProfileLut && wanted == m_ProfileLutBaked )
            return true;

        // The table is a TEXTURE, and the old one may still sit in a descriptor of a frame in flight —
        // the same reason the scatter target idles before it is replaced. Curves are dragged rarely
        // enough that a device idle is the honest cost; a per-frame rebake would not be.
        if ( m_ProfileLut )
            Renderer::GetInstance().WaitDeviceIdle();

        const Core::Formats::Image2DSpecification spec{
             .Tag        = "CloudProfileLut",
             .Width      = kCloudProfileLutWidth,
             .Height     = kCloudProfileLutTypes,
             .Format     = Core::Formats::ImageFormat::RGBA8F,
             .Mips       = 1u,
             .Data       = BuildCloudProfileLut( m_Data ),
             .Usage      = Core::Formats::Image2DUsage::Image2D,
             .Properties = Core::Formats::Sample,
        };

        m_ProfileLut = Image2D::Create( spec, nullptr );
        if ( !m_ProfileLut )
        {
            LOG_ERROR( "[Clouds] The {}x{} cloud profile table could not be created; the cloud layer will "
                       "not render for this view.",
                       kCloudProfileLutWidth, kCloudProfileLutTypes );
            m_ResourcesFailed = true;
            return false;
        }

        m_ProfileLutBaked = wanted;
        LOG_INFO( "[Clouds] Baked the {}x{} cloud profile table: {} authored vertical forms along the "
                  "Cloud Type axis.",
                  kCloudProfileLutWidth, kCloudProfileLutTypes, kCloudProfileLutTypes );
        return true;
    }

    void VolumetricCloudRenderer::DispatchWeather()
    {
        DESERT_PROFILE_SCOPE( "Clouds: WeatherMap" );

        auto& renderer = Renderer::GetInstance();

        renderer.ComputeImageBeginWrite( m_WeatherMap.get() );
        renderer.ComputeImageBeginWrite( m_ProfileMap.get() );
        m_WeatherPipeline->SetOutput( kCloudWeatherOutputBinding, m_WeatherMap.get(), 0 );
        m_WeatherPipeline->SetOutput( kCloudProfileOutputBinding, m_ProfileMap.get(), 0 );
        m_WeatherPipeline->SetStorageBuffer( kCloudParamsBinding, m_ParamsBuffer.get() );
        renderer.DispatchComputeInFrame( m_WeatherPipeline.get(), GroupCount( kWeatherMapSize ),
                                         GroupCount( kWeatherMapSize ), 1 );
        renderer.ComputeImageEndWrite( m_ProfileMap.get() );
        renderer.ComputeImageEndWrite( m_WeatherMap.get() );
    }

    void VolumetricCloudRenderer::DispatchShadowMap( const CloudNoiseSet& noise )
    {
        DESERT_PROFILE_SCOPE( "Clouds: ShadowMap" );

        const auto* camera = m_SceneRenderer->GetMainCamera();

        // The centre and the extent the march will project with. Pushed from ONE place to both passes in
        // the same frame: if they ever disagreed, every shadow would land somewhere other than the cloud
        // that cast it — a coordinate bug wearing a lighting bug's clothes.
        CloudShadowPush push{};
        push.Centre = glm::vec4( camera->GetPosition(), CloudShadowExtentOf( m_Data ) );

        auto& renderer = Renderer::GetInstance();

        renderer.ComputeImageBeginWrite( m_CloudShadowMap.get() );
        m_ShadowPipeline->SetOutput( kCloudShadowOutputBinding, m_CloudShadowMap.get(), 0 );
        m_ShadowPipeline->SetStorageBuffer( kCloudParamsBinding, m_ParamsBuffer.get() );
        m_ShadowPipeline->SetInput( kCloudShapeNoiseBinding, noise.ShapeNoise.get() );
        m_ShadowPipeline->SetInput( kCloudDetailNoiseBinding, noise.DetailNoise.get() );
        m_ShadowPipeline->SetInput( kCloudCurlNoiseBinding, noise.CurlNoise.get() );
        m_ShadowPipeline->SetInput( kCloudWeatherMapBinding, m_WeatherMap.get() );
        m_ShadowPipeline->SetInput( kCloudProfileMapBinding, m_ProfileMap.get() );
        m_ShadowPipeline->SetInput( kCloudProfileLutBinding, m_ProfileLut.get() );
        m_ShadowPipeline->SetPushConstants( &push, static_cast<uint32_t>( sizeof( push ) ) );

        renderer.DispatchComputeInFrame( m_ShadowPipeline.get(), GroupCount( kCloudShadowMapSize ),
                                         GroupCount( kCloudShadowMapSize ), 1 );
        renderer.ComputeImageEndWrite( m_CloudShadowMap.get() );
    }

    void VolumetricCloudRenderer::DispatchRaymarch( const CloudNoiseSet& noise, Image2D* depthImage,
                                                    bool checkerboard )
    {
        DESERT_PROFILE_SCOPE( "Clouds: Raymarch" );

        const auto* camera = m_SceneRenderer->GetMainCamera();

        const glm::mat4 viewProjection = camera->GetProjectionMatrix() * camera->GetViewMatrix();

        CloudRaymarchPush push{};
        push.InverseViewProjection = glm::inverse( viewProjection );
        push.CameraPosition        = glm::vec4( camera->GetPosition(), static_cast<float>( m_FrameIndex ) );
        push.Flags                 = glm::vec4( checkerboard ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f );

        auto& renderer = Renderer::GetInstance();

        // Present the DEPTH attachment to a compute sampler and hand it back afterwards. Its tracked
        // layout is DEPTH_STENCIL_ATTACHMENT_OPTIMAL, which is not a legal layout for a combined image
        // sampler, and SetInput binds the tracked layout verbatim.
        renderer.ComputeImageBeginRead( depthImage );
        renderer.ComputeImageBeginWrite( m_ScatterImage.get() );
        renderer.ComputeImageBeginWrite( m_DepthGuideImage.get() );

        m_RaymarchPipeline->SetOutput( kCloudScatterOutputBinding, m_ScatterImage.get(), 0 );
        m_RaymarchPipeline->SetOutput( kCloudDepthGuideBinding, m_DepthGuideImage.get(), 0 );
        m_RaymarchPipeline->SetStorageBuffer( kSkyPayloadBinding, m_SceneRenderer->GetAtmosphere().ParamsBuffer );
        m_RaymarchPipeline->SetStorageBuffer( kCloudParamsBinding, m_ParamsBuffer.get() );
        m_RaymarchPipeline->SetInput( kCloudShapeNoiseBinding, noise.ShapeNoise.get() );
        m_RaymarchPipeline->SetInput( kCloudDetailNoiseBinding, noise.DetailNoise.get() );
        m_RaymarchPipeline->SetInput( kCloudCurlNoiseBinding, noise.CurlNoise.get() );
        m_RaymarchPipeline->SetInput( kCloudWeatherMapBinding, m_WeatherMap.get() );
        m_RaymarchPipeline->SetInput( kCloudProfileMapBinding, m_ProfileMap.get() );
        m_RaymarchPipeline->SetInput( kCloudProfileLutBinding, m_ProfileLut.get() );
        m_RaymarchPipeline->SetInput( kCloudSceneDepthBinding, depthImage );
        // Always bound, even when the march will not read it: a declared sampler with no image is an
        // invalid descriptor set, not an unused one.
        m_RaymarchPipeline->SetInput( kCloudShadowMapBinding,
                                      m_CloudShadowMap ? m_CloudShadowMap.get() : m_WeatherMap.get() );
        m_RaymarchPipeline->SetPushConstants( &push, static_cast<uint32_t>( sizeof( push ) ) );

        renderer.DispatchComputeInFrame( m_RaymarchPipeline.get(), GroupCount( m_ScatterWidth ),
                                         GroupCount( m_ScatterHeight ), 1 );

        renderer.ComputeImageEndWrite( m_DepthGuideImage.get() );
        renderer.ComputeImageEndWrite( m_ScatterImage.get() );
        renderer.ComputeImageEndRead( depthImage );
    }

    void VolumetricCloudRenderer::DispatchTemporalResolve( bool checkerboard )
    {
        DESERT_PROFILE_SCOPE( "Clouds: TemporalResolve" );

        const auto* camera = m_SceneRenderer->GetMainCamera();

        const glm::mat4 projection     = camera->GetProjectionMatrix();
        const glm::mat4 view           = camera->GetViewMatrix();
        const glm::mat4 viewProjection = projection * view;

        // Flip FIRST: after this, m_HistoryWrite names the image this frame resolves into, which is also
        // the image the composite magnifies and the image the next frame will read as history. One index
        // instead of two, and the composite never has to guess which half of the pair is current.
        m_HistoryWrite ^= 1u;

        Image2D* write = m_HistoryImages[m_HistoryWrite].get();
        Image2D* read  = m_HistoryImages[m_HistoryWrite ^ 1u].get();

        // The SAME m_FrameIndex the raymarch pushed this frame — it is incremented after both stages,
        // so the two cannot disagree about which checkerboard half is fresh.
        const CloudTemporalPush push =
             MakeCloudTemporalPush( projection, view, m_PreviousViewProjection, camera->GetPosition(),
                                    m_HistoryFilled, checkerboard, m_FrameIndex );

        auto& renderer = Renderer::GetInstance();

        renderer.ComputeImageBeginWrite( write );

        m_TemporalPipeline->SetOutput( kCloudResolvedOutputBinding, write, 0 );
        m_TemporalPipeline->SetStorageBuffer( kCloudParamsBinding, m_ParamsBuffer.get() );
        m_TemporalPipeline->SetInput( kCloudCurrentFrameBinding, m_ScatterImage.get() );
        m_TemporalPipeline->SetInput( kCloudHistoryBinding, read );
        m_TemporalPipeline->SetPushConstants( &push, static_cast<uint32_t>( sizeof( push ) ) );

        renderer.DispatchComputeInFrame( m_TemporalPipeline.get(), GroupCount( m_ScatterWidth ),
                                         GroupCount( m_ScatterHeight ), 1 );

        renderer.ComputeImageEndWrite( write );

        // Only now is there a resolved frame to reproject FROM, and only now does the camera it was
        // rendered with become "the previous camera". Setting either earlier would reproject the first
        // blended frame against a camera that never produced an image.
        m_PreviousViewProjection = viewProjection;
        m_HistoryFilled          = true;
    }

    void VolumetricCloudRenderer::ExecuteInFrame()
    {
        DESERT_PROFILE_SCOPE( "Clouds: ExecuteInFrame" );

        m_HasFrameResult  = false;
        m_CompositeSource = CloudCompositeSource::Raymarch;

        if ( !m_Present || !m_Data.Enabled || !m_WeatherPipeline || !m_RaymarchPipeline || !m_TemporalPipeline ||
             !m_ParamsBuffer )
            return;

        // Checked here rather than inside the dispatches: without a camera there is no frame, and letting
        // each stage discover that separately is how m_HasFrameResult ends up true for a frame in which
        // nothing was dispatched, leaving the composite to magnify whatever was in the target before.
        const auto* camera = m_SceneRenderer->GetMainCamera();
        if ( !camera )
            return;

        const AtmosphereEnv& atmosphere = m_SceneRenderer->GetAtmosphere();
        if ( !atmosphere.Valid || !atmosphere.ParamsBuffer )
        {
            // No invented sun and no default sky: without an atmosphere there is no answer to "how
            // bright is the sun" that the sky would agree with, and a cloud lit by a guess is worse
            // than no cloud.
            if ( !m_AtmosphereWarned )
            {
                LOG_WARN( "[Clouds] The scene has a Volumetric Clouds component but no enabled Sky "
                          "Atmosphere component to light it; the cloud layer is not drawn." );
                m_AtmosphereWarned = true;
            }
            return;
        }
        m_AtmosphereWarned = false;

        const auto* noise =
             CloudNoiseVolumes::Get().Find( MakeCloudNoiseKey( m_Data.ShapeSeed, m_Data.DetailSeed ) );
        if ( !noise )
        {
            if ( !m_NoiseWarned )
            {
                LOG_WARN( "[Clouds] The noise volumes for shape seed {} / detail seed {} are not "
                          "available (see the [CloudNoise] log above); the cloud layer is not drawn.",
                          m_Data.ShapeSeed, m_Data.DetailSeed );
                m_NoiseWarned = true;
            }
            return;
        }
        m_NoiseWarned = false;

        const auto target = m_TargetFramebuffer.lock();
        if ( !target || target->GetDepthAttachmentCount() == 0 )
            return;

        if ( !EnsureResources( target->GetFramebufferWidth(), target->GetFramebufferHeight() ) )
            return;

        const CloudGpuPayload payload =
             PackCloudParams( m_Data, atmosphere, m_SceneRenderer->GetWind(), m_SceneRenderer->GetWind().Time );
        m_ParamsBuffer->SetData( &payload, static_cast<uint32_t>( sizeof( payload ) ) );

        // S1. The map is a function of the Weather group alone, so it is rebuilt when those fields
        // change and at no other time.
        const WeatherFingerprint wanted = FingerprintOf( m_Data );
        if ( !m_WeatherValid || !( wanted == m_WeatherBaked ) )
        {
            DispatchWeather();
            m_WeatherBaked = wanted;
            m_WeatherValid = true;
        }

        // S1b. The shadow map follows the weather map and precedes the march that reads it. Rebuilt
        // every frame: it is centred on the camera and parameterised on the sun, and both move.
        if ( m_CloudShadowMap && m_ShadowPipeline && m_Data.CloudShadowMap )
            DispatchShadowMap( *noise );

        // S3's history is decided BEFORE S2 runs: at Full resolution the march visits only half the
        // pixels each frame (the checkerboard), and it may do that only when the resolve that fills in
        // the other half is actually going to run — which is known here and nowhere later.
        bool historyReady = false;
        if ( CloudTemporalUsesHistory( m_Data.TemporalMode ) )
            historyReady = EnsureHistory();
        else
            ReleaseHistory();

        const bool checkerboard =
             CloudCheckerboardActive( m_Data.ResolutionScale, m_Data.TemporalMode, historyReady );

        // S2.
        DispatchRaymarch( *noise, target->GetDepthAttachmentImage().get(), checkerboard );

        // S3. Not a branch inside the resolve shader — a stage that either happens or does not. With
        // Temporal Mode = Off nothing is dispatched, no history is held, and the composite is pointed at
        // the image the march just wrote, so what reaches the screen is the marched frame bit for bit.
        if ( historyReady )
            DispatchTemporalResolve( checkerboard );

        m_CompositeSource = CloudSelectCompositeSource( m_Data.TemporalMode, historyReady );

        // Wrapped, not free-running: the index rides in a float push constant, and past 2^24 the
        // increment stops changing it — the jitter pattern would then freeze into a fixed dither. The
        // length must stay EVEN: the checkerboard is phased by this index's parity, and an odd wrap
        // would hand the same half of the pixels to the march two frames in a row once per cycle.
        m_FrameIndex     = ( m_FrameIndex + 1 ) % kJitterSequenceLength;
        m_HasFrameResult = true;
    }

    void VolumetricCloudRenderer::RegisterPasses( RenderGraphBuilder& builder )
    {
        const auto target = m_TargetFramebuffer.lock();
        if ( !target || !m_CompositePipeline )
            return;

        RenderGraphBuilder::PassConfig config;
        config.Name        = "VolumetricCloudComposite";
        config.Phase       = RenderPhase::Transparency;
        config.ExecuteFunc = [this]()
        {
            if ( !m_HasFrameResult || !m_ScatterImage || !m_DepthGuideImage || !m_CompositeMaterial )
                return;

            const Image2D* resolved = m_CompositeSource == CloudCompositeSource::TemporalHistory
                                           ? m_HistoryImages[m_HistoryWrite].get()
                                           : m_ScatterImage.get();

            m_CompositeMaterial->Bind( resolved, m_DepthGuideImage.get() );
            Renderer::GetInstance().SubmitFullscreenQuad( m_CompositePipeline.get(),
                                                          m_CompositeMaterial->GetMaterialExecutor() );
        };
        config.PipelineSpec      = m_CompositePipeline->GetSpecification();
        config.TargetFramebuffer = target;
        config.Dependencies      = { RenderPassDependency( RenderPhase::Geometry ) };

        // Clouds are the FAR FIELD of this phase and say so themselves, instead of relying on where
        // their system happens to sit in SceneRenderer::Init. Everything else in Transparency — sparks,
        // smoke, an emitter right in front of the camera — is nearer and must paint OVER them.
        config.OrderInPhase = RenderPassOrder::FarField;

        builder.AddPass( config );
    }
} // namespace Desert::Graphic::System
