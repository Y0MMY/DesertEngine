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

        // Length of the per-frame jitter sequence. Any small number does: the dither only has to differ
        // from frame to frame for the temporal stage to average it away.
        constexpr uint32_t kJitterSequenceLength = 64;

        constexpr const char* kWeatherShaderName   = "CloudWeather";
        constexpr const char* kRaymarchShaderName  = "CloudRaymarch";
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
        m_RaymarchPipeline.reset();
        m_CompositePipeline.reset();
        m_CompositeMaterial.reset();
        m_WeatherMap.reset();
        m_ScatterImage.reset();
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
        m_RaymarchPipeline = makeCompute( kRaymarchShaderName );

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

        return m_WeatherPipeline && m_RaymarchPipeline && m_CompositePipeline;
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

        const uint32_t scatterW = CloudScaledExtent( width, m_Data.ResolutionScale );
        const uint32_t scatterH = CloudScaledExtent( height, m_Data.ResolutionScale );

        if ( m_ScatterImage && scatterW == m_ScatterWidth && scatterH == m_ScatterHeight &&
             m_Data.ResolutionScale == m_ScatterScale )
            return true;

        // The old image may still be referenced by descriptors of frames in flight. This is the same
        // reason the sky bake idles before swapping its cubes.
        if ( m_ScatterImage )
            Renderer::GetInstance().WaitDeviceIdle();

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

        m_ScatterWidth   = scatterW;
        m_ScatterHeight  = scatterH;
        m_ScatterScale   = m_Data.ResolutionScale;
        m_HasFrameResult = false;

        // The cost is announced, not discovered in a memory graph later. One line per allocation, and
        // an allocation only happens on a resolution or tier change.
        LOG_INFO( "[Clouds] Scatter target {}x{} RGBA16F ({:.2f} MiB) + weather map {}x{} RGBA8 "
                  "({:.2f} MiB) for a {}x{} view.",
                  scatterW, scatterH,
                  BytesToMiB( Core::Formats::CalculateImageSize( scatterW, scatterH,
                                                                 Core::Formats::ImageFormat::RGBA16F ) ),
                  kWeatherMapSize, kWeatherMapSize,
                  BytesToMiB( Core::Formats::CalculateImageSize( kWeatherMapSize, kWeatherMapSize,
                                                                 Core::Formats::ImageFormat::RGBA8F ) ),
                  width, height );

        return true;
    }

    void VolumetricCloudRenderer::DispatchWeather()
    {
        DESERT_PROFILE_SCOPE( "Clouds: WeatherMap" );

        auto& renderer = Renderer::GetInstance();

        renderer.ComputeImageBeginWrite( m_WeatherMap.get() );
        m_WeatherPipeline->SetOutput( kCloudWeatherOutputBinding, m_WeatherMap.get(), 0 );
        m_WeatherPipeline->SetStorageBuffer( kCloudParamsBinding, m_ParamsBuffer.get() );
        renderer.DispatchComputeInFrame( m_WeatherPipeline.get(), GroupCount( kWeatherMapSize ),
                                         GroupCount( kWeatherMapSize ), 1 );
        renderer.ComputeImageEndWrite( m_WeatherMap.get() );
    }

    void VolumetricCloudRenderer::DispatchRaymarch( const CloudNoiseSet& noise, Image2D* depthImage )
    {
        DESERT_PROFILE_SCOPE( "Clouds: Raymarch" );

        const auto* camera = m_SceneRenderer->GetMainCamera();
        if ( !camera )
            return;

        const glm::mat4 viewProjection = camera->GetProjectionMatrix() * camera->GetViewMatrix();

        CloudRaymarchPush push{};
        push.InverseViewProjection = glm::inverse( viewProjection );
        push.CameraPosition        = glm::vec4( camera->GetPosition(), static_cast<float>( m_FrameIndex ) );

        auto& renderer = Renderer::GetInstance();

        // Present the DEPTH attachment to a compute sampler and hand it back afterwards. Its tracked
        // layout is DEPTH_STENCIL_ATTACHMENT_OPTIMAL, which is not a legal layout for a combined image
        // sampler, and SetInput binds the tracked layout verbatim.
        renderer.ComputeImageBeginRead( depthImage );
        renderer.ComputeImageBeginWrite( m_ScatterImage.get() );

        m_RaymarchPipeline->SetOutput( kCloudScatterOutputBinding, m_ScatterImage.get(), 0 );
        m_RaymarchPipeline->SetStorageBuffer( kSkyPayloadBinding, m_SceneRenderer->GetAtmosphere().ParamsBuffer );
        m_RaymarchPipeline->SetStorageBuffer( kCloudParamsBinding, m_ParamsBuffer.get() );
        m_RaymarchPipeline->SetInput( kCloudShapeNoiseBinding, noise.ShapeNoise.get() );
        m_RaymarchPipeline->SetInput( kCloudDetailNoiseBinding, noise.DetailNoise.get() );
        m_RaymarchPipeline->SetInput( kCloudCurlNoiseBinding, noise.CurlNoise.get() );
        m_RaymarchPipeline->SetInput( kCloudWeatherMapBinding, m_WeatherMap.get() );
        m_RaymarchPipeline->SetInput( kCloudSceneDepthBinding, depthImage );
        m_RaymarchPipeline->SetPushConstants( &push, static_cast<uint32_t>( sizeof( push ) ) );

        renderer.DispatchComputeInFrame( m_RaymarchPipeline.get(), GroupCount( m_ScatterWidth ),
                                         GroupCount( m_ScatterHeight ), 1 );

        renderer.ComputeImageEndWrite( m_ScatterImage.get() );
        renderer.ComputeImageEndRead( depthImage );
    }

    void VolumetricCloudRenderer::ExecuteInFrame()
    {
        DESERT_PROFILE_SCOPE( "Clouds: ExecuteInFrame" );

        m_HasFrameResult = false;

        if ( !m_Present || !m_Data.Enabled || !m_WeatherPipeline || !m_RaymarchPipeline || !m_ParamsBuffer )
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

        // S2.
        DispatchRaymarch( *noise, target->GetDepthAttachmentImage().get() );

        // Wrapped, not free-running: the index rides in a float push constant, and past 2^24 the
        // increment stops changing it — the jitter pattern would then freeze into a fixed dither.
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
            if ( !m_HasFrameResult || !m_ScatterImage || !m_CompositeMaterial )
                return;
            m_CompositeMaterial->Bind( m_ScatterImage.get() );
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
