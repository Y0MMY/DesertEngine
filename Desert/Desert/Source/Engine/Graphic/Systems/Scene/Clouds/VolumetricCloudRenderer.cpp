#include "VolumetricCloudRenderer.hpp"

#include <Engine/Graphic/Clouds/CloudLayerAspect.hpp>
#include <Engine/Graphic/Clouds/CloudMarchScale.hpp>
#include <Engine/Graphic/Clouds/CloudNoiseRules.hpp>
#include <Engine/Graphic/Clouds/CloudNoiseVolumes.hpp>
#include <Engine/Graphic/Clouds/CloudWeatherScale.hpp>
#include <Engine/Graphic/FallbackTextures.hpp>
#include <Engine/Graphic/RenderGraphSort.hpp>
#include <Engine/Graphic/SkyPayload.hpp>
#include <Engine/Graphic/RenderPhase.hpp>
#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Core/Camera.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <Common/Core/Logger.hpp>
#include <Common/Core/Profiler.hpp>

#include <algorithm>

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

        // The WORLD shadow map's resolution. The same 512 as the map above, over the same extent, for the
        // same reason: at the default 30 km that is ~117 m a texel, and a cloud shadow on the ground has
        // no detail finer than that to lose — the deck's own features are hundreds of metres across.
        constexpr uint32_t kCloudWorldShadowMapSize = 512;

        constexpr const char* kWeatherShaderName     = "CloudWeather";
        constexpr const char* kShadowShaderName      = "CloudShadowMap";
        constexpr const char* kWorldShadowShaderName = "CloudWorldShadowMap";
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

        // The hero-cloud instances. Allocated at its full size here and not lazily: it is 640 bytes, the
        // descriptor has to be bound on every dispatch whatever the scene holds (an unbound declared
        // binding is an invalid set), and the count that decides how much of it the shader reads lives in
        // the parameter block. Non-persistent for the same reason the parameter buffer is — one copy per
        // (frame x recording renderer slot), or the Details mesh preview would overwrite the viewport's.
        m_VolumeInstanceBuffer = ShaderResources::StorageBuffer::Create(
             "CloudVolumeInstances", kMaxCloudVolumeInstances * sizeof( CloudVolumeInstance ),
             kCloudVolumeInstanceBinding, /*persistent=*/false );
        if ( !m_VolumeInstanceBuffer )
            return Common::MakeError( "VolumetricCloudRenderer: could not create the hero-cloud instance "
                                      "buffer" );

        m_CompositeMaterial = std::make_unique<MaterialVolumetricClouds>();
        return BOOLSUCCESS;
    }

    void VolumetricCloudRenderer::Shutdown()
    {
        m_WeatherPipeline.reset();
        m_ShadowPipeline.reset();
        m_WorldShadowPipeline.reset();
        for ( auto& pipeline : m_RaymarchPipelines )
            pipeline.reset();
        m_TemporalPipeline.reset();
        m_CompositePipeline.reset();
        m_CompositeMaterial.reset();
        m_WeatherMap.reset();
        m_ProfileMap.reset();
        m_ProfileLut.reset();
        m_CloudShadowMap.reset();
        m_WorldShadowMap.reset();
        m_WorldShadow = CloudWorldShadowInput{};
        m_ScatterImage.reset();
        m_DepthGuideImage.reset();
        m_HistoryImages[0].reset();
        m_HistoryImages[1].reset();
        m_HistoryFilled = false;
        m_ParamsBuffer.reset();

        // Give every atlas tile back before the atlas itself goes. Its own last-lease path is what frees
        // the 32 MiB image; dropping the leases silently would leave that image alive until the process
        // exits, which is exactly what closing a scene view is supposed to undo.
        for ( const uint64_t key : m_VolumeLeases )
            m_VolumeAtlas.Release( key );
        m_VolumeLeases.clear();
        m_VolumePlacements.clear();
        m_VolumeCounts = CloudVoxelCounts{};
        m_VolumeInstanceBuffer.reset();
    }

    bool VolumetricCloudRenderer::CreatePipelines()
    {
        const auto shaderService = Runtime::ResourceRegistry::GetShaderService();
        if ( !shaderService )
            return false;

        const auto makeCompute =
             [&shaderService]( const char* name, std::vector<ShaderSpecializationConstant> specialization,
                               const std::string& debugName ) -> std::shared_ptr<ComputePipeline>
        {
            const auto shader = shaderService->GetByName( name );
            if ( !shader )
            {
                LOG_ERROR( "[Clouds] Compute shader '{}' is not registered. Expected "
                           "Editor/Resources/Shaders/Programs/Clouds/{}.shader.",
                           name, name );
                return nullptr;
            }
            auto pipeline = ComputePipeline::Create(
                 { .Shader = shader, .DebugName = debugName, .Specialization = std::move( specialization ) } );
            if ( !pipeline )
                return nullptr;
            // Create() allocates the object, Invalidate() builds the Vulkan pipeline. Both are needed;
            // this is the same two-line idiom as every other compute call site in the engine.
            pipeline->Invalidate();
            return pipeline;
        };

        m_WeatherPipeline     = makeCompute( kWeatherShaderName, {}, kWeatherShaderName );
        m_ShadowPipeline      = makeCompute( kShadowShaderName, {}, kShadowShaderName );
        m_WorldShadowPipeline = makeCompute( kWorldShadowShaderName, {}, kWorldShadowShaderName );
        m_TemporalPipeline    = makeCompute( kTemporalShaderName, {}, kTemporalShaderName );

        // One raymarch pipeline per layer count. They share the shader, the SPIR-V and the cache entry;
        // what the specialization constant changes is which of the two segment loops survives compilation.
        for ( uint32_t layers = 1; layers <= kCloudMaxLayers; ++layers )
        {
            m_RaymarchPipelines[CloudRaymarchLayerCount( layers ) - 1] =
                 makeCompute( kRaymarchShaderName,
                              { ShaderSpecializationConstant{ .Id    = kCloudLayerCountConstantId,
                                                              .Value = static_cast<int32_t>( layers ) } },
                              std::string( kRaymarchShaderName ) + "_" + std::to_string( layers ) + "Layer" );
        }

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

        return m_WeatherPipeline && RaymarchPipelineFor( 1 ) && RaymarchPipelineFor( kCloudMaxLayers ) &&
               m_TemporalPipeline && m_CompositePipeline;
    }

    ComputePipeline* VolumetricCloudRenderer::RaymarchPipelineFor( uint32_t liveLayers ) const
    {
        // CloudRaymarchLayerCount, not a clamp written out here: it is the same pure function the
        // CloudPayload suite asserts can never come back BELOW the count CloudPackPayload wrote into the
        // buffer, which is the failure mode with no error message — a second layer packed and never
        // marched.
        return m_RaymarchPipelines[CloudRaymarchLayerCount( liveLayers ) - 1].get();
    }

    void VolumetricCloudRenderer::SetCloudSettings( const CloudLayerSet&         layers,
                                                    const CloudVolumePlacements& volumes )
    {
        m_Layers           = layers;
        m_VolumePlacements = volumes;
    }

    void VolumetricCloudRenderer::SetWorldShadowRequest( bool cast, float strength )
    {
        // A strength of zero is the same statement as the toggle being off — the shader's own OFF path is
        // `strength <= 0` and nothing else — so the two are collapsed here, once, rather than left for
        // five consumers to each decide about.
        m_WorldShadowStrength  = glm::clamp( strength, 0.0f, 1.0f );
        m_WorldShadowRequested = cast && m_WorldShadowStrength > 0.0f;
    }

    bool VolumetricCloudRenderer::EnsureWorldShadowMap()
    {
        if ( m_WorldShadowMap )
            return true;
        if ( m_WorldShadowFailed || !m_WorldShadowPipeline )
            return false;

        const Core::Formats::Image2DSpecification spec{
             .Tag        = "CloudWorldShadowMap",
             .Width      = kCloudWorldShadowMapSize,
             .Height     = kCloudWorldShadowMapSize,
             .Format     = Core::Formats::ImageFormat::RGBA16F,
             .Mips       = 1u,
             .Usage      = Core::Formats::Image2DUsage::Image2D,
             .Properties = Core::Formats::Storage | Core::Formats::Sample,
        };

        m_WorldShadowMap = Image2D::Create( spec, nullptr );
        if ( !m_WorldShadowMap )
        {
            // Not fatal to the frame: every consumer's shader reads a strength of 0 and returns full sun,
            // which is exactly the picture the scene had before the light asked for cloud shadows. Said
            // once, with its numbers, rather than leaving an artist to wonder why the toggle does nothing.
            LOG_ERROR(
                 "[Clouds] The {}x{} RGBA16F world cloud-shadow map ({:.2f} MiB) could not be "
                 "created; Cast Cloud Shadows has no effect for this view.",
                 kCloudWorldShadowMapSize, kCloudWorldShadowMapSize,
                 BytesToMiB( Core::Formats::CalculateImageSize( kCloudWorldShadowMapSize, kCloudWorldShadowMapSize,
                                                                Core::Formats::ImageFormat::RGBA16F ) ) );
            m_WorldShadowFailed = true;
            return false;
        }

        LOG_INFO(
             "[Clouds] World cloud-shadow map {}x{} RGBA16F ({:.2f} MiB) allocated — the deck now "
             "shadows terrain, meshes and grass.",
             kCloudWorldShadowMapSize, kCloudWorldShadowMapSize,
             BytesToMiB( Core::Formats::CalculateImageSize( kCloudWorldShadowMapSize, kCloudWorldShadowMapSize,
                                                            Core::Formats::ImageFormat::RGBA16F ) ) );
        return true;
    }

    void VolumetricCloudRenderer::UpdateVolumeInstances()
    {
        DESERT_PROFILE_SCOPE( "Clouds: HeroVolumes" );

        auto* service = Runtime::ResourceRegistry::GetCloudVolumeService();

        std::vector<CloudVolumeInstance> instances;
        std::vector<uint64_t>            leases;
        instances.reserve( m_VolumePlacements.size() );
        leases.reserve( m_VolumePlacements.size() );

        int32_t shadowCount = 0;

        for ( const auto& placement : m_VolumePlacements )
        {
            const uint64_t key = static_cast<uint64_t>( placement.Data.Volume );

            // A handle that cannot be resolved or leased is reported ONCE, because the alternative is the
            // same line at 60 Hz, which is the same as no line at all. Returns true the first time only.
            const auto firstFailureFor = [this]( uint64_t handle )
            {
                if ( std::find( m_VolumeFailures.begin(), m_VolumeFailures.end(), handle ) !=
                     m_VolumeFailures.end() )
                    return false;
                m_VolumeFailures.push_back( handle );
                return true;
            };

            const auto asset = service->Get( Assets::AssetHandle( key ) );
            if ( !asset || !asset->IsReadyForUse() )
            {
                if ( firstFailureFor( key ) )
                    LOG_ERROR( "[CloudVolumes] The .dvol behind asset handle {} is not loaded, so the hero "
                               "cloud referencing it is not rendered. See the [CloudVolume] load line above "
                               "for the reason the file was rejected.",
                               key );
                continue;
            }

            // Acquire BEFORE the previous frame's leases are released, so a volume that survives the
            // frame is never dropped to zero references and re-uploaded. Acquire on an already-resident
            // key is a refcount bump and no image work at all, which is what makes moving a hero cloud
            // around free.
            const auto tile = m_VolumeAtlas.Acquire( key, asset->GetVolume() );
            if ( !tile.IsSuccess() )
            {
                if ( firstFailureFor( key ) )
                    LOG_ERROR( "[CloudVolumes] {}", tile.GetError() );
                continue;
            }

            leases.push_back( key );
            instances.push_back( MakeCloudVolumeInstance( placement.WorldTransform, asset->GetVolume().Header,
                                                          tile.GetValue(), placement.Data.DensityScale,
                                                          placement.Data.DetailTypeBias ) );

            // The placements arrive sorted shadow-casters-first, so the casters are a prefix — counted
            // here rather than trusted, because a gap would make the shadow pass march a cloud whose
            // author switched the flag off.
            if ( placement.Data.CastsCloudShadow && shadowCount == static_cast<int32_t>( instances.size() ) - 1 )
                ++shadowCount;
        }

        // Now the previous frame's leases go, one Release per Acquire. Anything still referenced was
        // acquired again above, so its refcount does not reach zero here.
        for ( const uint64_t key : m_VolumeLeases )
            m_VolumeAtlas.Release( key );
        m_VolumeLeases = std::move( leases );

        m_VolumeCounts =
             CloudVoxelCounts{ .Total = static_cast<int32_t>( instances.size() ), .Shadow = shadowCount };

        if ( !instances.empty() && m_VolumeInstanceBuffer )
            m_VolumeInstanceBuffer->SetData(
                 instances.data(), static_cast<uint32_t>( instances.size() * sizeof( CloudVolumeInstance ) ) );
    }

    bool VolumetricCloudRenderer::EnsureResources( uint32_t width, uint32_t height )
    {
        if ( m_ResourcesFailed )
            return false;

        // ONE SLICE PER CLOUD LAYER, always allocated at the full depth whatever the scene holds: adding
        // a second layer must not reallocate a 2 MiB image mid-session, and a slice nobody marches costs
        // one megabyte that is never fetched.
        if ( !m_WeatherMap )
        {
            const Core::Formats::Image3DSpecification spec{
                 .Tag        = "CloudWeatherMap",
                 .Width      = kWeatherMapSize,
                 .Height     = kWeatherMapSize,
                 .Depth      = kCloudMaxLayers,
                 .Format     = Core::Formats::ImageFormat::RGBA8F,
                 .Properties = Core::Formats::Storage | Core::Formats::Sample,
            };
            m_WeatherMap = Image3D::Create( spec );
            if ( !m_WeatherMap )
            {
                LOG_ERROR( "[Clouds] The {}x{}x{} weather map ({:.2f} MiB) could not be created; the cloud "
                           "layer will not render for this view.",
                           kWeatherMapSize, kWeatherMapSize, kCloudMaxLayers,
                           BytesToMiB( Core::Formats::CalculateImageSize( kWeatherMapSize, kWeatherMapSize,
                                                                          kCloudMaxLayers,
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
            const Core::Formats::Image3DSpecification spec{
                 .Tag        = "CloudProfileMap",
                 .Width      = kWeatherMapSize,
                 .Height     = kWeatherMapSize,
                 .Depth      = kCloudMaxLayers,
                 .Format     = Core::Formats::ImageFormat::RGBA8F,
                 .Properties = Core::Formats::Storage | Core::Formats::Sample,
            };
            m_ProfileMap = Image3D::Create( spec );
            if ( !m_ProfileMap )
            {
                LOG_ERROR( "[Clouds] The {}x{}x{} profile map ({:.2f} MiB) could not be created; the cloud "
                           "layer will not render for this view.",
                           kWeatherMapSize, kWeatherMapSize, kCloudMaxLayers,
                           BytesToMiB( Core::Formats::CalculateImageSize( kWeatherMapSize, kWeatherMapSize,
                                                                          kCloudMaxLayers,
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
            const Core::Formats::Image3DSpecification spec{
                 .Tag        = "CloudShadowMap",
                 .Width      = kCloudShadowMapSize,
                 .Height     = kCloudShadowMapSize,
                 .Depth      = kCloudMaxLayers,
                 .Format     = Core::Formats::ImageFormat::RGBA16F,
                 .Properties = Core::Formats::Storage | Core::Formats::Sample,
            };
            m_CloudShadowMap = Image3D::Create( spec );
            if ( !m_CloudShadowMap )
            {
                // Not fatal: the march falls back to the cone for every sample, which is what it did
                // before this map existed. Say so once rather than rendering slowly and silently.
                LOG_WARN( "[Clouds] The {}x{}x{} cloud shadow map could not be created; the raymarch will "
                          "cone-march every shaded sample instead.",
                          kCloudShadowMapSize, kCloudShadowMapSize, kCloudMaxLayers );
            }
        }

        const uint32_t scatterW = CloudScaledExtent( width, m_Layers.Primary().ResolutionScale );
        const uint32_t scatterH = CloudScaledExtent( height, m_Layers.Primary().ResolutionScale );

        if ( m_ScatterImage && m_DepthGuideImage && scatterW == m_ScatterWidth && scatterH == m_ScatterHeight &&
             m_Layers.Primary().ResolutionScale == m_ScatterScale )
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
        m_ScatterScale   = m_Layers.Primary().ResolutionScale;
        m_HasFrameResult = false;

        // The cost is announced, not discovered in a memory graph later. One line per allocation, and
        // an allocation only happens on a resolution or tier change. The total is the same arithmetic
        // CloudScaledImageBytes states and the unit test pins, so the log and the test cannot drift.
        LOG_INFO( "[Clouds] Scatter target {}x{} RGBA16F ({:.2f} MiB) + depth guide RGBA8 ({:.2f} MiB) + "
                  "weather map {}x{}x{} RGBA8 ({:.2f} MiB) for a {}x{} view. Scaled imagery totals "
                  "{:.2f} MiB with the temporal history and {:.2f} MiB without it.",
                  scatterW, scatterH,
                  BytesToMiB( Core::Formats::CalculateImageSize( scatterW, scatterH,
                                                                 Core::Formats::ImageFormat::RGBA16F ) ),
                  BytesToMiB( Core::Formats::CalculateImageSize( scatterW, scatterH,
                                                                 Core::Formats::ImageFormat::RGBA8F ) ),
                  kWeatherMapSize, kWeatherMapSize, kCloudMaxLayers,
                  BytesToMiB( Core::Formats::CalculateImageSize( kWeatherMapSize, kWeatherMapSize, kCloudMaxLayers,
                                                                 Core::Formats::ImageFormat::RGBA8F ) ),
                  width, height,
                  BytesToMiB( CloudScaledImageBytes( width, height, m_Layers.Primary().ResolutionScale,
                                                     ECS::CloudTemporalMode::Reprojection ) ),
                  BytesToMiB( CloudScaledImageBytes( width, height, m_Layers.Primary().ResolutionScale,
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
        // ONE SLICE PER LAYER. The nine curve vectors the table is baked from are per-layer fields, so a
        // deck's cumulus family and a sheet's flat one are two different tables; concatenating them into
        // one volume keeps it one sampler and one fetch.
        std::array<ProfileFingerprint, kCloudMaxLayers> wanted{};
        for ( uint32_t i = 0; i < m_Layers.Count; ++i )
            wanted[i] = ProfileFingerprintOf( m_Layers.Layers[i] );

        if ( m_ProfileLut && wanted == m_ProfileLutBaked && m_Layers.Count == m_ProfileLutBakedCount )
            return true;

        // The table is a TEXTURE, and the old one may still sit in a descriptor of a frame in flight —
        // the same reason the scatter target idles before it is replaced. Curves are dragged rarely
        // enough that a device idle is the honest cost; a per-frame rebake would not be.
        if ( m_ProfileLut )
            Renderer::GetInstance().WaitDeviceIdle();

        // Slice-major, which is how a 3D image is uploaded: layer 0's whole 128x6 table, then layer 1's.
        // A slice with no layer behind it is baked from the default component rather than left at zero —
        // an all-zero profile is a table that says "no cloud at any height", and if it were ever sampled
        // the symptom would be an empty sky with nothing to look at.
        std::vector<unsigned char> texels;
        texels.reserve( static_cast<std::size_t>( kCloudProfileLutWidth ) * kCloudProfileLutTypes * 4u *
                        kCloudMaxLayers );
        for ( uint32_t i = 0; i < kCloudMaxLayers; ++i )
        {
            const std::vector<unsigned char> slice =
                 BuildCloudProfileLut( i < m_Layers.Count ? m_Layers.Layers[i] : ECS::VolumetricCloudData{} );
            texels.insert( texels.end(), slice.begin(), slice.end() );
        }

        const Core::Formats::Image3DSpecification spec{
             .Tag        = "CloudProfileLut",
             .Width      = kCloudProfileLutWidth,
             .Height     = kCloudProfileLutTypes,
             .Depth      = kCloudMaxLayers,
             .Format     = Core::Formats::ImageFormat::RGBA8F,
             .Data       = std::move( texels ),
             .Properties = Core::Formats::Sample,
        };

        m_ProfileLut = Image3D::Create( spec );
        if ( !m_ProfileLut )
        {
            LOG_ERROR( "[Clouds] The {}x{}x{} cloud profile table could not be created; the cloud layer "
                       "will not render for this view.",
                       kCloudProfileLutWidth, kCloudProfileLutTypes, kCloudMaxLayers );
            m_ResourcesFailed = true;
            return false;
        }

        m_ProfileLutBaked      = wanted;
        m_ProfileLutBakedCount = m_Layers.Count;
        LOG_INFO( "[Clouds] Baked the {}x{}x{} cloud profile table: {} authored vertical forms along the "
                  "Cloud Type axis, for {} cloud layer(s).",
                  kCloudProfileLutWidth, kCloudProfileLutTypes, kCloudMaxLayers, kCloudProfileLutTypes,
                  m_Layers.Count );
        return true;
    }

    void VolumetricCloudRenderer::DispatchWeather()
    {
        DESERT_PROFILE_SCOPE( "Clouds: WeatherMap" );

        // The one place that knows both the tile and the layer, and the only moment worth saying it: the
        // map is re-baked when a Weather field changes, not per frame. A tile far from the one the
        // layer's altitude asks for is not a resource failure and must not be silently corrected — it is
        // a sky that will read as a dense horizon band under empty blue, and the artist gets the numbers
        // and the value that fixes it rather than a mystery. See CloudWeatherScale.hpp.
        // Checked PER LAYER: the relation is between a tile and the altitude of the layer that tiles the
        // sky with it, so a deck and a sheet each have their own answer and each can be wrong on its own.
        for ( uint32_t i = 0; i < m_Layers.Count; ++i )
        {
            const ECS::VolumetricCloudData& layer = m_Layers.Layers[i];
            if ( CloudWeatherTileIsPlausible( layer.WeatherTileSize, layer.LayerBottomAltitude,
                                              layer.LayerThickness ) )
                continue;

            const float wanted = CloudAutoWeatherTileSize( layer.LayerBottomAltitude, layer.LayerThickness );
            LOG_WARN( "[Clouds] Layer {}: Weather Tile Size {:.1f} km over a layer at {:.2f}-{:.2f} km: a "
                      "ground camera sees {:.1f} coverage cells across the sky above 20 degrees, not "
                      "{:.1f}. The layer's altitude asks for {:.1f} km.",
                      i, Common::Units::ToMetres( layer.WeatherTileSize ) / 1000.0f,
                      Common::Units::ToMetres( layer.LayerBottomAltitude ) / 1000.0f,
                      Common::Units::ToMetres( layer.LayerBottomAltitude + layer.LayerThickness ) / 1000.0f,
                      kCloudWeatherCellsOverhead * wanted / layer.WeatherTileSize, kCloudWeatherCellsOverhead,
                      Common::Units::ToMetres( wanted ) / 1000.0f );
        }

        // The layer's own PROPORTIONS, which the two relations either side of this loop leave open: each
        // ties the layer to something else, neither says how tall it may be for how wide it is. A layer
        // taller than its own coverage cell is wide describes a convective tower, and a convective tower is
        // deep — so one that is not deep is a slab standing on end, and it renders as a ceiling overhead
        // rather than as clouds at altitude. That was the whole fair-weather family before this warning
        // existed. See CloudLayerAspect.hpp.
        for ( uint32_t i = 0; i < m_Layers.Count; ++i )
        {
            const ECS::VolumetricCloudData& layer = m_Layers.Layers[i];
            if ( CloudLayerAspectIsPlausible( layer.WeatherTileSize, layer.LayerThickness ) )
                continue;

            const float aspect = CloudLayerAspect( layer.WeatherTileSize, layer.LayerThickness );
            const float wanted =
                 CloudLayerThicknessForAspect( layer.WeatherTileSize, kCloudMinAspectBelowDeepConvection );
            LOG_WARN( "[Clouds] Layer {}: {:.2f} km thick under a {:.2f} km coverage cell — the clouds are "
                      "{:.2f}x as wide as they are tall, i.e. TALLER than wide, which only a cumulonimbus "
                      "is. This layer is not deep enough to be one ({:.2f} km against the {:.1f} km deep "
                      "convection needs), so it will read as a ceiling overhead rather than as clouds at "
                      "altitude. At this Weather Tile Size the layer has to be at most {:.2f} km thick.",
                      i, Common::Units::ToMetres( layer.LayerThickness ) / 1000.0f,
                      Common::Units::ToMetres( layer.WeatherTileSize / kCloudWeatherBasePeriod ) / 1000.0f, aspect,
                      Common::Units::ToMetres( layer.LayerThickness ) / 1000.0f,
                      Common::Units::ToMetres( kCloudDeepConvectionThickness ) / 1000.0f,
                      Common::Units::ToMetres( wanted ) / 1000.0f );
        }

        // And the OTHER scale relation, which a THIN layer walks into: the empty-space search's stride
        // against the layer's own thickness. Said here, beside the weather tile, because both are
        // properties of the layer's geometry rather than of the frame, and both are things an artist can
        // only diagnose from the numbers. See CloudMarchScale.hpp for what it costs to get wrong.
        for ( uint32_t i = 0; i < m_Layers.Count; ++i )
        {
            const ECS::VolumetricCloudData& layer = m_Layers.Layers[i];

            const CloudSearchAcrossLayer worst = CloudWorstSearchAcrossLayer(
                 layer.LayerBottomAltitude, layer.LayerThickness, layer.MinStepSize, layer.MaxStepSize,
                 layer.StepGrowthRate, layer.CoarseStepMultiplier );
            if ( worst.Samples >= kCloudMinSearchSamplesAcrossLayer )
                continue;

            // The ELEVATION is in the message because it is the actionable half. A high layer's worst
            // elevation is in the middle of the sky, not overhead, and an artist told only a sample count
            // would look up — at the one part of their sky where the number is fine.
            LOG_WARN( "[Clouds] Layer {}: the empty-space search takes {:.1f} samples across a {:.2f} km "
                      "layer at {:.0f} degrees of elevation, not the {:.1f} it needs. The search can then "
                      "stride over the layer entirely, and whether a ray notices it becomes a per-pixel "
                      "coin toss no temporal average removes. Lower Coarse Step Multiplier ({:.1f}), Step "
                      "Growth Rate ({:.4f}) or Max Step Size ({:.0f} m).",
                      i, worst.Samples, Common::Units::ToMetres( layer.LayerThickness ) / 1000.0f,
                      worst.ElevationDegrees, kCloudMinSearchSamplesAcrossLayer, layer.CoarseStepMultiplier,
                      layer.StepGrowthRate, Common::Units::ToMetres( layer.MaxStepSize ) );
        }

        auto& renderer = Renderer::GetInstance();

        renderer.ComputeImageBeginWrite( m_WeatherMap.get() );
        renderer.ComputeImageBeginWrite( m_ProfileMap.get() );
        m_WeatherPipeline->SetOutput( kCloudWeatherOutputBinding, m_WeatherMap.get(), 0 );
        m_WeatherPipeline->SetOutput( kCloudProfileOutputBinding, m_ProfileMap.get(), 0 );
        m_WeatherPipeline->SetStorageBuffer( kCloudParamsBinding, m_ParamsBuffer.get() );
        // ONE dispatch, one workgroup deep per layer: the shader reads its z as the layer index. Only the
        // live layers are baked — a slice with no layer behind it is never sampled, because the march's
        // own layer count (its specialization constant, RaymarchPipelineFor) comes from the same
        // m_Layers.Count this dispatch is sized by.
        renderer.DispatchComputeInFrame( m_WeatherPipeline.get(), GroupCount( kWeatherMapSize ),
                                         GroupCount( kWeatherMapSize ), m_Layers.Count );
        renderer.ComputeImageEndWrite( m_ProfileMap.get() );
        renderer.ComputeImageEndWrite( m_WeatherMap.get() );
    }

    void VolumetricCloudRenderer::BindHeroVolumes( ComputePipeline* pipeline )
    {
        // BOTH descriptors are bound on every dispatch, whether or not the scene placed a hero cloud: a
        // declared binding with nothing behind it is an invalid descriptor set, not an unused one — the
        // same rule the shadow-map and aerial-perspective slots live under. When there is no atlas the
        // engine's 1x1x1 fallback volume stands in; the shader never reads it, because
        // u_VoxelInstanceCount is 0 and the union's loop does not run.
        pipeline->SetStorageBuffer( kCloudVolumeInstanceBinding, m_VolumeInstanceBuffer.get() );
        pipeline->SetInput(
             kCloudVolumeAtlasBinding,
             m_VolumeAtlas.GetImage()
                  ? m_VolumeAtlas.GetImage().get()
                  : FallbackTextures::Get().GetFallbackTexture3D( Core::Formats::ImageFormat::RGBA8F ).get() );
    }

    void VolumetricCloudRenderer::DispatchShadowMap( const CloudNoiseSet& noise )
    {
        DESERT_PROFILE_SCOPE( "Clouds: ShadowMap" );

        const auto* camera = m_SceneRenderer->GetMainCamera();

        // The centre the march will project with. The EXTENT is per layer and rides in the parameter
        // block, which both passes read from the same member — a stronger guarantee than two call sites
        // agreeing, and if they ever disagreed every shadow would land somewhere other than the cloud
        // that cast it: a coordinate bug wearing a lighting bug's clothes.
        CloudShadowPush push{};
        push.Centre = glm::vec4( camera->GetPosition(), 0.0f );

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
        BindHeroVolumes( m_ShadowPipeline.get() );
        m_ShadowPipeline->SetPushConstants( &push, static_cast<uint32_t>( sizeof( push ) ) );

        // One slice per layer that READS it, and no further. A layer self-shadows with its own mass and
        // nothing else: the four heights a texel stores are heights inside THAT shell, and a shared map
        // would have to spend three of them on the empty air between a deck and a sheet. Cross-layer
        // shadowing — a high sheet dimming the deck under it — is therefore not modelled; for the optical
        // depths a cirrus sheet actually carries it is a sub-ten-per-cent effect, well inside the ambient
        // term's own noise.
        //
        // The depth stops at the last layer whose own Cloud Shadow Map is on, because this pass is 24
        // samples of density at every one of 512x512 texels and a slice nobody fetches is that cost for
        // nothing. The layers below it are still dispatched even if they are off: one dispatch has one
        // depth, and the alternative is one dispatch per slice.
        renderer.DispatchComputeInFrame( m_ShadowPipeline.get(), GroupCount( kCloudShadowMapSize ),
                                         GroupCount( kCloudShadowMapSize ), m_ShadowSlices );
        renderer.ComputeImageEndWrite( m_CloudShadowMap.get() );
    }

    void VolumetricCloudRenderer::DispatchWorldShadowMap( const CloudNoiseSet& noise )
    {
        DESERT_PROFILE_SCOPE( "Clouds: WorldShadowMap" );

        const auto* camera = m_SceneRenderer->GetMainCamera();

        // ONE map for the whole sky, so it needs ONE extent, and it takes the PRIMARY (lowest) layer's.
        // The deck nearest the ground is the one whose shadows the ground actually shows; giving the map
        // the largest extent in the scene instead would let a cirrus sheet with a 200 km reach spend the
        // deck's texels on air. Layers above it are still marched — they are simply clipped to the same
        // footprint, which is the footprint anything on the ground can see a shadow inside of anyway.
        const float extent = CloudShadowExtentOf( m_Layers.Primary() );

        CloudWorldShadowPush push{};
        push.CentreExtent = glm::vec4( camera->GetPosition(), extent );

        auto& renderer = Renderer::GetInstance();

        renderer.ComputeImageBeginWrite( m_WorldShadowMap.get() );
        m_WorldShadowPipeline->SetOutput( kCloudWorldShadowOutputBinding, m_WorldShadowMap.get(), 0 );
        m_WorldShadowPipeline->SetStorageBuffer( kCloudParamsBinding, m_ParamsBuffer.get() );
        m_WorldShadowPipeline->SetInput( kCloudShapeNoiseBinding, noise.ShapeNoise.get() );
        m_WorldShadowPipeline->SetInput( kCloudDetailNoiseBinding, noise.DetailNoise.get() );
        m_WorldShadowPipeline->SetInput( kCloudCurlNoiseBinding, noise.CurlNoise.get() );
        m_WorldShadowPipeline->SetInput( kCloudWeatherMapBinding, m_WeatherMap.get() );
        m_WorldShadowPipeline->SetInput( kCloudProfileMapBinding, m_ProfileMap.get() );
        m_WorldShadowPipeline->SetInput( kCloudProfileLutBinding, m_ProfileLut.get() );
        BindHeroVolumes( m_WorldShadowPipeline.get() );
        m_WorldShadowPipeline->SetPushConstants( &push, static_cast<uint32_t>( sizeof( push ) ) );

        renderer.DispatchComputeInFrame( m_WorldShadowPipeline.get(), GroupCount( kCloudWorldShadowMapSize ),
                                         GroupCount( kCloudWorldShadowMapSize ), 1 );
        renderer.ComputeImageEndWrite( m_WorldShadowMap.get() );

        // PUBLISHED WITH THE FRAME IT WAS TRACED IN. The centre, the sun and the extent travel with the
        // image rather than being re-derived by each consumer from the camera and the light — the map is
        // a projection, and a consumer projecting through a different frame would put every shadow on the
        // ground somewhere other than under the cloud that cast it.
        m_WorldShadow.Map          = m_WorldShadowMap.get();
        m_WorldShadow.CentreExtent = push.CentreExtent;
        m_WorldShadow.SunStrength =
             glm::vec4( m_SceneRenderer->GetAtmosphere().SunDirection, m_WorldShadowStrength );
    }

    void VolumetricCloudRenderer::DispatchRaymarch( const CloudNoiseSet& noise, Image2D* depthImage,
                                                    bool checkerboard )
    {
        DESERT_PROFILE_SCOPE( "Clouds: Raymarch" );

        // The pipeline whose specialization constant says how many layers to march. Selected from the same
        // m_Layers.Count that CloudPackPayload writes into the buffer as LayerCount — one number, two
        // consequences. Refusing here rather than dispatching a pipeline that could not be built is the
        // difference between a frame with no clouds and a frame with someone else's descriptors.
        ComputePipeline* raymarch = RaymarchPipelineFor( m_Layers.Count );
        if ( !raymarch )
        {
            LOG_ERROR( "[Clouds] No raymarch pipeline for {} live layer(s); the cloud pass is skipped this "
                       "frame.",
                       m_Layers.Count );
            return;
        }

        const auto* camera = m_SceneRenderer->GetMainCamera();

        const glm::mat4 viewProjection = camera->GetProjectionMatrix() * camera->GetViewMatrix();

        // The sky this view published THIS frame. Both handles are null exactly when the quantity behind
        // them does not exist — the artistic gradient, a sky switched off, or a fill that could not
        // allocate — which is AtmosphereEnv's own contract, and the two gates below are the only place
        // the march asks about the sky model at all.
        const AtmosphereEnv& atmosphere = m_SceneRenderer->GetAtmosphere();
        const bool           apActive   = atmosphere.AerialPerspectiveVolume != nullptr;
        const bool           skyLight   = atmosphere.DistantSkyLight != nullptr;

        CloudRaymarchPush push{};
        push.InverseViewProjection = glm::inverse( viewProjection );
        push.CameraPosition        = glm::vec4( camera->GetPosition(), static_cast<float>( m_FrameIndex ) );
        push.Flags                 = glm::vec4( checkerboard ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f );
        push.Atmosphere =
             glm::vec4( atmosphere.AerialPerspectiveDepthKm, atmosphere.AerialPerspectiveViewDistanceScale,
                        apActive ? 1.0f : 0.0f, skyLight ? 1.0f : 0.0f );

        auto& renderer = Renderer::GetInstance();

        // Present the DEPTH attachment to a compute sampler and hand it back afterwards. Its tracked
        // layout is DEPTH_STENCIL_ATTACHMENT_OPTIMAL, which is not a legal layout for a combined image
        // sampler, and SetInput binds the tracked layout verbatim.
        renderer.ComputeImageBeginRead( depthImage );
        renderer.ComputeImageBeginWrite( m_ScatterImage.get() );
        renderer.ComputeImageBeginWrite( m_DepthGuideImage.get() );

        raymarch->SetOutput( kCloudScatterOutputBinding, m_ScatterImage.get(), 0 );
        raymarch->SetOutput( kCloudDepthGuideBinding, m_DepthGuideImage.get(), 0 );
        raymarch->SetStorageBuffer( kSkyPayloadBinding, m_SceneRenderer->GetAtmosphere().ParamsBuffer );
        raymarch->SetStorageBuffer( kCloudParamsBinding, m_ParamsBuffer.get() );
        raymarch->SetInput( kCloudShapeNoiseBinding, noise.ShapeNoise.get() );
        raymarch->SetInput( kCloudDetailNoiseBinding, noise.DetailNoise.get() );
        raymarch->SetInput( kCloudCurlNoiseBinding, noise.CurlNoise.get() );
        raymarch->SetInput( kCloudWeatherMapBinding, m_WeatherMap.get() );
        raymarch->SetInput( kCloudProfileMapBinding, m_ProfileMap.get() );
        raymarch->SetInput( kCloudProfileLutBinding, m_ProfileLut.get() );
        raymarch->SetInput( kCloudSceneDepthBinding, depthImage );
        // Always bound, even when the march will not read it: a declared sampler with no image is an
        // invalid descriptor set, not an unused one.
        raymarch->SetInput( kCloudShadowMapBinding,
                            m_CloudShadowMap ? m_CloudShadowMap.get() : m_WeatherMap.get() );
        // The physical atmosphere's two images, bound on exactly the same terms and for exactly the same
        // reason: the engine's 1x1(x1) fallbacks stand in when the sky did not publish them, and
        // push.Atmosphere's gates are what keep the shader from reading either.
        raymarch->SetInput(
             kCloudAerialPerspectiveBinding,
             apActive ? atmosphere.AerialPerspectiveVolume
                      : FallbackTextures::Get().GetFallbackTexture3D( Core::Formats::ImageFormat::RGBA8F ).get() );
        raymarch->SetInput(
             kCloudDistantSkyLightBinding,
             skyLight ? atmosphere.DistantSkyLight
                      : FallbackTextures::Get().GetFallbackTexture2D( Core::Formats::ImageFormat::RGBA8F ).get() );
        BindHeroVolumes( raymarch );
        raymarch->SetPushConstants( &push, static_cast<uint32_t>( sizeof( push ) ) );

        renderer.DispatchComputeInFrame( raymarch, GroupCount( m_ScatterWidth ), GroupCount( m_ScatterHeight ),
                                         1 );

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

    void VolumetricCloudRenderer::PrepareInFrame()
    {
        DESERT_PROFILE_SCOPE( "Clouds: PrepareInFrame" );

        // Stated fresh every frame, before anything can fail: a light that stopped asking for world
        // shadows, a scene whose cloud layer was deleted, or a map that could not be allocated must all
        // leave the consumers reading strength 0 — and they read THIS.
        m_WorldShadow = CloudWorldShadowInput{};

        // THE WHOLE COST OF THE FEATURE IS BEHIND THIS LINE. With no light asking for world shadows
        // nothing is prepared here at all: the frame's hero-cloud diff, parameter upload and weather bake
        // all still happen exactly once, in ExecuteInFrame, exactly as they did before this pass existed.
        if ( !m_WorldShadowRequested )
            return;

        const FramePreparation prep = PrepareFrame();
        if ( !prep.Ready )
            return;

        if ( !EnsureWorldShadowMap() )
            return;

        // S1c. Dispatched HERE, before the render graph records, and not in the cloud stage after it:
        // the terrain draws inside that graph and the deferred lighting pass runs immediately after it,
        // so a map traced in the cloud stage would be a map they could only read one frame late.
        DispatchWorldShadowMap( *prep.Noise );
    }

    VolumetricCloudRenderer::FramePreparation VolumetricCloudRenderer::PrepareFrame()
    {
        FramePreparation prep;

        // THE HERO CLOUDS, before every early-out below and not after them. Two reasons, and both are
        // about giving memory back: the union happens INSIDE the march, so a layer that is not marching
        // has no use for atlas tiles at all — clearing the placements here is what turns unticking Enabled
        // into 32 MiB returned rather than 32 MiB held for a layer that draws nothing. And the atlas image
        // has to exist before either dispatch binds it, which is a statement about ordering that is easier
        // to keep true at the top of the function than in the middle of it.
        if ( m_Layers.Empty() )
            m_VolumePlacements.clear();
        UpdateVolumeInstances();

        if ( m_Layers.Empty() || !m_WeatherPipeline || !RaymarchPipelineFor( m_Layers.Count ) ||
             !m_TemporalPipeline || !m_ParamsBuffer )
            return prep;

        // Checked here rather than inside the dispatches: without a camera there is no frame, and letting
        // each stage discover that separately is how m_HasFrameResult ends up true for a frame in which
        // nothing was dispatched, leaving the composite to magnify whatever was in the target before.
        const auto* camera = m_SceneRenderer->GetMainCamera();
        if ( !camera )
            return prep;

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
            return prep;
        }
        m_AtmosphereWarned = false;

        // ONE NOISE SET FOR EVERY LAYER, from the PRIMARY layer's seeds. The three volumes are a shape
        // VOCABULARY - a Perlin-Worley lattice and a detail field - not weather, and what makes a sheet
        // look nothing like the deck under it is its own tile sizes, coverage, type curves and erosion,
        // every one of which is per layer. Leasing a second 20 MiB set per layer to reseed a lattice that
        // is then sampled at a different scale anyway is a cost with no picture behind it. The upper
        // layers' Shape Seed and Detail Seed are therefore not read; the Details panel says so.
        const ECS::VolumetricCloudData& primary = m_Layers.Primary();

        const auto* noise =
             CloudNoiseVolumes::Get().Find( MakeCloudNoiseKey( primary.ShapeSeed, primary.DetailSeed ) );
        if ( !noise )
        {
            if ( !m_NoiseWarned )
            {
                LOG_WARN( "[Clouds] The noise volumes for shape seed {} / detail seed {} are not "
                          "available (see the [CloudNoise] log above); the cloud layer is not drawn.",
                          primary.ShapeSeed, primary.DetailSeed );
                m_NoiseWarned = true;
            }
            return prep;
        }
        m_NoiseWarned = false;

        const auto target = m_TargetFramebuffer.lock();
        if ( !target || target->GetDepthAttachmentCount() == 0 )
            return prep;

        if ( !EnsureResources( target->GetFramebufferWidth(), target->GetFramebufferHeight() ) )
            return prep;

        const CloudGpuPayload payload = PackCloudParams( m_Layers, atmosphere, m_SceneRenderer->GetWind(),
                                                         m_SceneRenderer->GetWind().Time, m_VolumeCounts );
        m_ParamsBuffer->SetData( &payload, static_cast<uint32_t>( sizeof( payload ) ) );

        // S1. The map is a function of the Weather group alone, so it is rebuilt when those fields
        // change and at no other time. One fingerprint per layer, and any of them moving rebakes every
        // slice - one dispatch writes them all, and splitting it per slice would optimise a pass whose
        // rate is "an artist dragged a slider".
        std::array<WeatherFingerprint, kCloudMaxLayers> wanted{};
        for ( uint32_t i = 0; i < m_Layers.Count; ++i )
            wanted[i] = FingerprintOf( m_Layers.Layers[i] );

        if ( !m_WeatherValid || wanted != m_WeatherBaked || m_Layers.Count != m_WeatherBakedCount )
        {
            DispatchWeather();
            m_WeatherBaked      = wanted;
            m_WeatherBakedCount = m_Layers.Count;
            m_WeatherValid      = true;
        }

        prep.Noise = noise;
        prep.Ready = true;
        return prep;
    }

    void VolumetricCloudRenderer::ExecuteInFrame()
    {
        DESERT_PROFILE_SCOPE( "Clouds: ExecuteInFrame" );

        m_HasFrameResult  = false;
        m_CompositeSource = CloudCompositeSource::Raymarch;

        // Idempotent, and already run this frame when a light asked for world shadows — see the note on
        // FramePreparation. Calling it here unconditionally is what keeps the march's own preconditions in
        // one place instead of two that could drift apart.
        const FramePreparation prep = PrepareFrame();
        if ( !prep.Ready )
            return;

        const ECS::VolumetricCloudData& primary = m_Layers.Primary();

        // S1b. The shadow map follows the weather map and precedes the march that reads it. Rebuilt
        // every frame: it is centred on the camera and parameterised on the sun, and both move.
        // Any layer asking for the map is enough to run the pass: it fills every live slice in one
        // dispatch, and a layer whose own Cloud Shadow Map is off reads none of it (u_CloudShadowEnabled
        // gates the read per layer, so it cone-marches instead).
        m_ShadowSlices = 0;
        for ( uint32_t i = 0; i < m_Layers.Count; ++i )
        {
            if ( m_Layers.Layers[i].CloudShadowMap )
                m_ShadowSlices = i + 1;
        }

        if ( m_CloudShadowMap && m_ShadowPipeline && m_ShadowSlices > 0 )
            DispatchShadowMap( *prep.Noise );

        // S3's history is decided BEFORE S2 runs: at Full resolution the march visits only half the
        // pixels each frame (the checkerboard), and it may do that only when the resolve that fills in
        // the other half is actually going to run — which is known here and nowhere later.
        bool historyReady = false;
        if ( CloudTemporalUsesHistory( primary.TemporalMode ) )
            historyReady = EnsureHistory();
        else
            ReleaseHistory();

        const bool checkerboard =
             CloudCheckerboardActive( primary.ResolutionScale, primary.TemporalMode, historyReady );

        // S2. The target is re-locked here rather than carried out of PrepareFrame: a weak_ptr's lock is
        // the statement that the framebuffer is still alive, and a statement made in another function is
        // a statement about another moment.
        const auto target = m_TargetFramebuffer.lock();
        if ( !target || target->GetDepthAttachmentCount() == 0 )
            return;

        DispatchRaymarch( *prep.Noise, target->GetDepthAttachmentImage().get(), checkerboard );

        // S3. Not a branch inside the resolve shader — a stage that either happens or does not. With
        // Temporal Mode = Off nothing is dispatched, no history is held, and the composite is pointed at
        // the image the march just wrote, so what reaches the screen is the marched frame bit for bit.
        if ( historyReady )
            DispatchTemporalResolve( checkerboard );

        m_CompositeSource = CloudSelectCompositeSource( primary.TemporalMode, historyReady );

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
