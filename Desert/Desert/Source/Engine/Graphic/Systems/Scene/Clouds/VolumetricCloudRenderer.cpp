#include "VolumetricCloudRenderer.hpp"

#include <Engine/Core/Camera.hpp>
#include <Engine/Graphic/FallbackTextures.hpp>
#include <Engine/Graphic/RenderGraphSort.hpp>
#include <Engine/Graphic/RenderPhase.hpp>
#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <Common/Core/Logger.hpp>
#include <Common/Core/Profiler.hpp>

namespace Desert::Graphic::System
{
    namespace
    {
        // 8x8 for the screen-space march, matching the LocalSize the shader declares. 64 invocations is
        // inside every implementation's guaranteed maximum and the dispatch bounds-checks, so any target
        // size is fine.
        constexpr uint32_t kMarchWorkGroupSize = 8;

        constexpr const char* kMarchShaderName     = "CloudRaymarch";
        constexpr const char* kResolveShaderName   = "CloudTemporalResolve";
        constexpr const char* kCompositeShaderName = "CloudComposite";

        constexpr uint32_t GroupCount( uint32_t extent, uint32_t groupSize )
        {
            return ( extent + groupSize - 1 ) / groupSize;
        }

        // Half resolution, rounded UP. Rounding down would leave the right and bottom column of the frame
        // uncovered by any cloud texel, which the composite's filter then stretches — a one-pixel smear
        // along two edges of the screen that is very hard to attribute. Applied TWICE to reach the trace's
        // quarter resolution, and written that way rather than as a single round-up by four because the
        // invariant the jitter depends on is "the trace grid covers the HALF grid in 2x2 blocks". Chaining
        // the same function states that invariant; a separate QuarterExtent would be a second expression
        // that has to keep agreeing with this one.
        constexpr uint32_t HalfExtent( uint32_t extent )
        {
            return ( extent + 1u ) / 2u;
        }

        double BytesToMiB( uint64_t bytes )
        {
            return static_cast<double>( bytes ) / ( 1024.0 * 1024.0 );
        }
    } // namespace

    VolumetricCloudRenderer::~VolumetricCloudRenderer() = default;

    Common::BoolResultStr VolumetricCloudRenderer::Initialize()
    {
        if ( !CreatePipelines() )
            return Common::MakeError( "VolumetricCloudRenderer: the cloud shaders could not be resolved "
                                      "(CloudRaymarch / CloudTemporalResolve / CloudComposite)" );

        // Non-persistent, so the backend keeps one copy per (frame x recording renderer slot) — the
        // Docs/RENDERER_FRAME_STATE.md rule. A shared buffer would let an asset-thumbnail renderer
        // overwrite the viewport's cloud parameters halfway through a frame.
        m_ParamsBuffer = ShaderResources::StorageBuffer::Create( "CloudParams", kCloudPayloadBytes,
                                                                 kCloudParamsBinding, /*persistent=*/false );
        if ( !m_ParamsBuffer )
            return Common::MakeError( "VolumetricCloudRenderer: could not create the cloud parameter buffer" );

        // The reconstruction's camera matrices, on the same terms and for the same reason. It is a buffer
        // rather than a push constant because two 4x4 matrices already fill the 128 bytes Vulkan
        // guarantees for push constants — see the comment on Graphic::CloudResolveParams.
        m_ResolveParamsBuffer = ShaderResources::StorageBuffer::Create(
             "CloudResolveParams", kCloudResolveParamsBytes, kCloudResolveParamsBinding, /*persistent=*/false );
        if ( !m_ResolveParamsBuffer )
            return Common::MakeError(
                 "VolumetricCloudRenderer: could not create the cloud reconstruction parameter buffer" );

        m_CompositeMaterial = std::make_unique<MaterialCloudComposite>();
        return BOOLSUCCESS;
    }

    void VolumetricCloudRenderer::Shutdown()
    {
        m_MarchPipeline.reset();
        m_ResolvePipeline.reset();
        m_CompositePipeline.reset();
        m_CompositeMaterial.reset();
        m_TraceImage.reset();
        m_TraceGuideImage.reset();
        for ( uint32_t i = 0; i < 2u; ++i )
        {
            m_HistoryImage[i].reset();
            m_HistoryGuideImage[i].reset();
        }
        m_ProfileTable.reset();
        for ( uint32_t slot = 0; slot < kCloudSpeciesSlots; ++slot )
            m_ProfileTypes[slot] = Assets::AssetHandle::Null();
        m_ProfileSpeciesCount = 0;
        m_ProfileGeneration   = 0;
        m_ParamsBuffer.reset();
        m_ResolveParamsBuffer.reset();
    }

    uint32_t VolumetricCloudRenderer::ResolveSpecies( CloudTypeShape ( &shapes )[kCloudSpeciesSlots],
                                                      Assets::AssetHandle ( &handles )[kCloudSpeciesSlots] ) const
    {
        auto* types = Runtime::ResourceRegistry::GetCloudTypeService();

        // The panel's four slots, in the order they are drawn. The one statement of that order.
        const Assets::AssetHandle authored[kCloudSpeciesSlots] = { m_Data.CloudType1, m_Data.CloudType2,
                                                                   m_Data.CloudType3, m_Data.CloudType4 };

        uint32_t count = 0;
        for ( uint32_t slot = 0; slot < kCloudSpeciesSlots; ++slot )
        {
            if ( authored[slot] == Assets::AssetHandle::Null() )
                continue;

            // DUPLICATES ARE DROPPED, and this is not tidiness. The same type twice is the same placement
            // field twice — same scale, same anisotropy — differing only by the slot's decorrelation
            // offset, so it is two independent skies of one kind of cloud laid over each other at twice
            // the cost of one and for no visible gain but a denser sky the Coverage slider no longer
            // describes. An artist who wants two of a kind wants two TYPES with different scales, and the
            // slot they freed is there for it.
            bool seen = false;
            for ( uint32_t taken = 0; taken < count; ++taken )
                seen = seen || handles[taken] == authored[slot];
            if ( seen )
                continue;

            handles[count] = authored[slot];
            shapes[count]  = types->GetShape( authored[slot] );
            ++count;
        }

        if ( count == 0 )
        {
            // ALL FOUR EMPTY IS A DOCUMENTED ANSWER, and it is the SAME answer an empty single slot gave
            // before there were four: one built-in cumulus congestus. A scene nobody has authored a type
            // for still has to have a sky.
            handles[0] = Assets::AssetHandle::Null();
            shapes[0]  = Assets::CloudTypeDefaultShape();
            count      = 1;
        }

        return count;
    }

    bool VolumetricCloudRenderer::EnsureProfileTable()
    {
        auto* types = Runtime::ResourceRegistry::GetCloudTypeService();

        CloudTypeShape      shapes[kCloudSpeciesSlots]{};
        Assets::AssetHandle handles[kCloudSpeciesSlots]{};
        const uint32_t      speciesCount = ResolveSpecies( shapes, handles );

        // TWO REASONS TO REBUILD, AND BOTH ARE ASKED ABOUT. The SET of handles changes when the artist
        // drops a different type into any slot, adds one or clears one; the SERVICE GENERATION changes
        // when a file behind a handle they already chose is edited and hot-reloaded. A cache keyed on the
        // handle alone would show the old numbers forever after a save, which is the difference between a
        // tool an artist iterates in and one they restart.
        const uint32_t generation = types->GetGeneration();

        bool sameSet =
             m_ProfileTable && m_ProfileSpeciesCount == speciesCount && m_ProfileGeneration == generation;
        for ( uint32_t slot = 0; sameSet && slot < speciesCount; ++slot )
            sameSet = m_ProfileTypes[slot] == handles[slot];

        if ( sameSet )
            return true;

        // A FRESH IMAGE RATHER THAN AN IN-PLACE UPLOAD, which is the same choice CloudNoiseService makes
        // and for the same reason: SetData writes into an image that frames still in flight may be
        // sampling, and the type changes when an artist touches a combo box — not often enough to be
        // worth the synchronisation argument. The old image goes through Image2D's deletion queue when
        // the last reference drops.
        if ( m_ProfileTable )
            Renderer::GetInstance().WaitDeviceIdle();

        const std::vector<float> texels = CloudBuildProfileTable( shapes, speciesCount );

        const Core::Formats::Image2DSpecification spec{
             .Tag        = "CloudProfileTable",
             .Width      = kCloudProfileTableAltitudeTexels,
             .Height     = kCloudProfileTablePatternTexels,
             .Format     = Core::Formats::ImageFormat::RGBA32F,
             .Mips       = 1u,
             .Data       = texels,
             .Usage      = Core::Formats::Image2DUsage::Image2D,
             .Properties = Core::Formats::Sample,
        };

        m_ProfileTable = Image2D::Create( spec, nullptr );
        if ( !m_ProfileTable )
        {
            m_ProfileSpeciesCount = 0;
            m_ProfileGeneration   = 0;
            LOG_ERROR( "[Clouds] The {}x{} RGBA32F vertical profile table for this layer's {} cloud type(s) "
                       "could not be created on the device; the clouds will not render for this view.",
                       kCloudProfileTableAltitudeTexels, kCloudProfileTablePatternTexels, speciesCount );
            return false;
        }

        for ( uint32_t slot = 0; slot < kCloudSpeciesSlots; ++slot )
            m_ProfileTypes[slot] = slot < speciesCount ? handles[slot] : Assets::AssetHandle::Null();
        m_ProfileSpeciesCount = speciesCount;
        m_ProfileGeneration   = generation;

        // EVERY SPECIES' OWN BAND IS PRINTED BESIDE THE ENVELOPE, not just the envelope. The containment
        // relation is the one this subsystem gets wrong silently — a type whose top is above the shell has
        // its anvil sliced off and nothing says so — and a log line that only prints the union cannot be
        // read against anything.
        const CloudEnvelopeKm envelope = CloudTypeSetEnvelopeKm( shapes, speciesCount );

        LOG_INFO( "[Clouds] Vertical profile table rebuilt for {} cloud type(s) — envelope {:.2f} to {:.2f} km, "
                  "{}x{} RGBA32F ({:.2f} MiB).",
                  speciesCount, envelope.BottomKm, envelope.TopKm, kCloudProfileTableAltitudeTexels,
                  kCloudProfileTablePatternTexels,
                  BytesToMiB( Core::Formats::CalculateImageSize( kCloudProfileTableAltitudeTexels,
                                                                 kCloudProfileTablePatternTexels,
                                                                 Core::Formats::ImageFormat::RGBA32F ) ) );

        for ( uint32_t slot = 0; slot < speciesCount; ++slot )
        {
            LOG_INFO( "[Clouds]   channel {} — type {}, {:.2f} to {:.2f} km, placement x{:.2f} stretched "
                      "x{:.2f} along the wind.",
                      slot, static_cast<uint64_t>( handles[slot] ), CloudTypeBaseKm( shapes[slot] ),
                      CloudTypeTopKm( shapes[slot] ), shapes[slot].PlacementScale,
                      shapes[slot].PlacementAnisotropy );
        }

        return true;
    }

    bool VolumetricCloudRenderer::CreatePipelines()
    {
        const auto shaderService = Runtime::ResourceRegistry::GetShaderService();
        if ( !shaderService )
            return false;

        const auto marchShader = shaderService->GetByName( kMarchShaderName );
        if ( !marchShader )
        {
            LOG_ERROR( "[Clouds] Compute shader '{}' is not registered. Expected "
                       "Editor/Resources/Shaders/Programs/Clouds/{}.shader.",
                       kMarchShaderName, kMarchShaderName );
            return false;
        }
        m_MarchPipeline = ComputePipeline::Create( { .Shader = marchShader, .DebugName = kMarchShaderName } );
        if ( !m_MarchPipeline )
            return false;
        m_MarchPipeline->Invalidate();

        const auto resolveShader = shaderService->GetByName( kResolveShaderName );
        if ( !resolveShader )
        {
            LOG_ERROR( "[Clouds] Compute shader '{}' is not registered. Expected "
                       "Editor/Resources/Shaders/Programs/Clouds/{}.shader.",
                       kResolveShaderName, kResolveShaderName );
            return false;
        }
        m_ResolvePipeline =
             ComputePipeline::Create( { .Shader = resolveShader, .DebugName = kResolveShaderName } );
        if ( !m_ResolvePipeline )
            return false;
        m_ResolvePipeline->Invalidate();

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

        // A fullscreen quad has no meaningful depth of its own; occlusion was resolved inside the march,
        // which cut every ray at the distance the depth attachment reported.
        spec.DepthTestEnabled  = false;
        spec.DepthWriteEnabled = false;
        spec.CullMode          = CullMode::None;
        spec.Topology          = PrimitiveTopology::Triangles;

        // scene = cloud.rgb * One + scene * cloud.a — the premultiplied over-operator with alpha carrying
        // TRANSMITTANCE. The march emits exactly that pair.
        spec.BlendEnable         = true;
        spec.SrcColorBlendFactor = BlendFactor::One;
        spec.DstColorBlendFactor = BlendFactor::SrcAlpha;

        // Replayed by ExecuteTransparency with a LOAD begin, so the pipeline is built against the
        // framebuffer's LOAD render pass.
        spec.UseLoadRenderPass = true;

        m_CompositePipeline = GraphicsPipeline::Create( spec );
        if ( m_CompositePipeline )
            m_CompositePipeline->Invalidate();

        return m_MarchPipeline && m_ResolvePipeline && m_CompositePipeline;
    }

    void VolumetricCloudRenderer::SetCloudSettings( bool present, const ECS::VolumetricCloudData& data,
                                                    const glm::vec3& windOffset )
    {
        m_Present    = present;
        m_Data       = data;
        m_WindOffset = windOffset;
    }

    bool VolumetricCloudRenderer::EnsureTraceTargets( uint32_t halfWidth, uint32_t halfHeight )
    {
        if ( m_TargetsFailed )
            return false;

        const bool allocated = m_TraceImage && m_TraceGuideImage && m_HistoryImage[0] && m_HistoryImage[1] &&
                               m_HistoryGuideImage[0] && m_HistoryGuideImage[1];
        if ( allocated && halfWidth == m_HalfWidth && halfHeight == m_HalfHeight )
            return true;

        // The old images may still be referenced by descriptors of frames in flight.
        if ( m_TraceImage || m_HistoryImage[0] )
            Renderer::GetInstance().WaitDeviceIdle();

        const uint32_t traceWidth  = HalfExtent( halfWidth );
        const uint32_t traceHeight = HalfExtent( halfHeight );

        // One helper for all six, because the only thing that differs between them is a size and a name.
        // Written as a lambda taking its parameters rather than capturing them: a parameter-less
        // multi-line lambda is one of the constructs clang-format 18 and 22 disagree about, and the CI
        // gate runs 18.
        auto createTarget = []( const char* tag, uint32_t width, uint32_t height )
        {
            const Core::Formats::Image2DSpecification spec{
                 .Tag        = tag,
                 .Width      = width,
                 .Height     = height,
                 .Format     = Core::Formats::ImageFormat::RGBA16F,
                 .Mips       = 1u,
                 .Usage      = Core::Formats::Image2DUsage::Image2D,
                 .Properties = Core::Formats::Storage | Core::Formats::Sample,
            };
            return Image2D::Create( spec, nullptr );
        };

        // The march's pair, at a QUARTER of the view. RGBA16F for the scatter because radiance is
        // pre-tonemap HDR; RGBA16F for the guide for a reason worth stating rather than discovering: the
        // guide carries TWO meaningful channels and Core::Formats::ImageFormat offers no two-channel float
        // format, so half of every texel is allocated and never written. Half a kilometre of distance
        // resolves to about thirty metres at this precision, two orders finer than the tenth-of-the-
        // distance threshold the composite compares against and three finer than the two-kilometre
        // disocclusion threshold the reconstruction compares against.
        m_TraceImage      = createTarget( "CloudTrace", traceWidth, traceHeight );
        m_TraceGuideImage = createTarget( "CloudTraceGuide", traceWidth, traceHeight );

        for ( uint32_t i = 0; i < 2u; ++i )
        {
            m_HistoryImage[i]      = createTarget( "CloudReconstruction", halfWidth, halfHeight );
            m_HistoryGuideImage[i] = createTarget( "CloudReconstructionGuide", halfWidth, halfHeight );
        }

        const double traceMiB = BytesToMiB(
             Core::Formats::CalculateImageSize( traceWidth, traceHeight, Core::Formats::ImageFormat::RGBA16F ) );
        const double halfMiB = BytesToMiB(
             Core::Formats::CalculateImageSize( halfWidth, halfHeight, Core::Formats::ImageFormat::RGBA16F ) );

        if ( !m_TraceImage || !m_TraceGuideImage || !m_HistoryImage[0] || !m_HistoryImage[1] ||
             !m_HistoryGuideImage[0] || !m_HistoryGuideImage[1] )
        {
            LOG_ERROR( "[Clouds] The reconstruction targets could not be created — trace {}x{} ({:.2f} MiB "
                       "each, scatter {}, guide {}), history {}x{} ({:.2f} MiB each, scatter {}/{}, guide "
                       "{}/{}); the clouds will not render for this view.",
                       traceWidth, traceHeight, traceMiB, m_TraceImage != nullptr, m_TraceGuideImage != nullptr,
                       halfWidth, halfHeight, halfMiB, m_HistoryImage[0] != nullptr, m_HistoryImage[1] != nullptr,
                       m_HistoryGuideImage[0] != nullptr, m_HistoryGuideImage[1] != nullptr );

            // Released together rather than left half standing: the pass writes all six or none, and a
            // trace image with no history beside it is a target nothing may reconstruct.
            m_TraceImage.reset();
            m_TraceGuideImage.reset();
            for ( uint32_t i = 0; i < 2u; ++i )
            {
                m_HistoryImage[i].reset();
                m_HistoryGuideImage[i].reset();
            }
            m_TargetsFailed = true;
            return false;
        }

        m_HalfWidth  = halfWidth;
        m_HalfHeight = halfHeight;

        // Every image is fresh, so whatever the resolve read last frame is gone. Saying so here rather
        // than at the call site is what keeps the flag true to the memory it describes: a resize is the
        // one event that invalidates the history without the camera moving at all.
        m_HistoryValid = false;

        // The cost is announced once, on the allocation, not discovered in a memory graph later. All six
        // are named and counted, because targets of the same size are exactly what a reader of a memory
        // graph would otherwise take for one measured several times.
        LOG_INFO( "[Clouds] Trace targets {}x{} RGBA16F ({:.2f} MiB each, scatter + guide) — a QUARTER of "
                  "the {}x{} view, one jittered sub-pixel per frame.",
                  traceWidth, traceHeight, traceMiB, halfWidth * 2u, halfHeight * 2u );
        LOG_INFO( "[Clouds] Reconstruction targets {}x{} RGBA16F ({:.2f} MiB each, scatter + guide, "
                  "ping-ponged) — {:.2f} MiB total for the pass.",
                  halfWidth, halfHeight, halfMiB, 2.0 * traceMiB + 4.0 * halfMiB );
        return true;
    }

    bool VolumetricCloudRenderer::EnsureNoiseVolume()
    {
        auto* service = Runtime::ResourceRegistry::GetCloudNoiseService();

        // Asked EVERY frame rather than cached behind a "have I got one" flag, because the answer changes
        // for three independent reasons now — the artist picks a different cloud TYPE, the type they
        // already picked is edited to name a different volume, or the volume itself is re-baked and
        // hot-reloaded. A flag would answer none of them. Both lookups are hash-map probes against a
        // handle; they are not worth a cache that can be wrong.
        //
        // THE VOLUME COMES THROUGH THE TYPE, which is the one structural change T1 made to this path: the
        // character of a cloud's edge is a property of what kind of cloud it is, so the type names it and
        // the layer does not. An empty slot on either side lands on the built-in default volume.
        //
        // ONE VOLUME FOR THE WHOLE LAYER, TAKEN FROM THE FIRST SLOT, AND THAT IS A LIMIT RATHER THAN A
        // DESIGN — it is stated here so the next person meets it in the code and not in a frame. There is
        // one `sampler3D` on the march, so four types in one sky share one volume, and a Cirrus in slot 2
        // beside a Cumulus in slot 1 is cut from the cumulus' volume. Two things make it survivable and
        // neither makes it right: the per-species DetailCharacter already chooses between the volume's
        // wispy and billowy PAIRS, which is most of what a type's edge is; and the quantile map that makes
        // `Coverage` mean one fraction of sky is calibrated to one distribution, so four volumes in one
        // layer would need four calibrations before they meant anything. Lifting it is a sampler array on
        // the march and a fetch indexed by the winning slot — a contained change, and not this task's.
        Assets::AssetHandle firstType = m_Data.CloudType1;
        if ( firstType == Assets::AssetHandle::Null() )
            firstType = m_Data.CloudType2;
        if ( firstType == Assets::AssetHandle::Null() )
            firstType = m_Data.CloudType3;
        if ( firstType == Assets::AssetHandle::Null() )
            firstType = m_Data.CloudType4;

        Image3D* volume =
             service->Get( Runtime::ResourceRegistry::GetCloudTypeService()->GetNoiseVolume( firstType ) );
        if ( !volume )
        {
            // The service has already logged which volume is missing and why. Latched so a scene with a
            // broken reference does not print once per frame forever.
            if ( !m_NoiseFailed )
            {
                m_NoiseFailed = true;
                LOG_ERROR( "[Clouds] No noise volume could be resolved for this layer; the clouds will not "
                           "render for this view until one is registered." );
            }
            return false;
        }

        // Cleared as soon as a volume does resolve: the failure above is a state of the SCENE, not of this
        // renderer, and dropping a project's clouds for the rest of the session because one scene was
        // opened with a stale reference is the kind of latch that reads as a broken build.
        m_NoiseFailed = false;

        m_NoiseVolume = volume;
        return true;
    }

    void VolumetricCloudRenderer::ExecuteInFrame()
    {
        DESERT_PROFILE_SCOPE( "Clouds: ExecuteInFrame" );

        m_HasFrameResult = false;

        if ( !m_MarchPipeline || !m_ResolvePipeline || !m_CompositePipeline || !m_ParamsBuffer ||
             !m_ResolveParamsBuffer )
            return;

        if ( !m_Present || !m_Data.Enabled )
            return;

        // Without a sky there is no sun to light the clouds and no ambient to fill their shadowed sides.
        // Marching anyway would draw black cut-outs across the frame, which is worse than drawing nothing
        // and much harder to diagnose — so the layer is simply absent, exactly as the sky's own contract
        // says a consumer must treat Valid == false.
        const AtmosphereEnv& atmosphere = m_SceneRenderer->GetAtmosphere();
        if ( !atmosphere.Valid )
            return;

        const auto* camera = m_SceneRenderer->GetMainCamera();
        if ( !camera )
            return;

        const auto target = m_TargetFramebuffer.lock();
        if ( !target || target->GetDepthAttachmentCount() == 0 )
            return;

        if ( !EnsureTraceTargets( HalfExtent( target->GetFramebufferWidth() ),
                                  HalfExtent( target->GetFramebufferHeight() ) ) )
            return;

        if ( !EnsureNoiseVolume() )
            return;

        if ( !EnsureProfileTable() )
            return;

        // RESOLVED AGAIN RATHER THAN CACHED FROM EnsureProfileTable. Two hash-map probes per slot against
        // handles that have not moved is not worth a member that can disagree with the table it was built
        // beside — and the one thing that must not diverge is exactly the set the table's channels are in.
        CloudTypeShape      shapes[kCloudSpeciesSlots]{};
        Assets::AssetHandle handles[kCloudSpeciesSlots]{};
        const uint32_t      speciesCount = ResolveSpecies( shapes, handles );

        const CloudGpuPayload payload = PackCloudParams( m_Data, shapes, speciesCount, atmosphere, m_WindOffset );
        m_ParamsBuffer->SetData( &payload, static_cast<uint32_t>( sizeof( payload ) ) );

        const glm::mat4     viewProjection = camera->GetProjectionMatrix() * camera->GetViewMatrix();
        const CloudSubPixel subPixel       = CloudTraceSubPixel( m_FrameIndex );

        CloudPush push{};
        push.InverseViewProjection = glm::inverse( viewProjection );
        push.CameraPosition = glm::vec4( camera->GetPosition(), static_cast<float>( m_FrameIndex & 0xFFFFu ) );
        push.Trace          = glm::vec4( static_cast<float>( subPixel.X ), static_cast<float>( subPixel.Y ),
                                         static_cast<float>( m_HalfWidth ), static_cast<float>( m_HalfHeight ) );

        auto& renderer = Renderer::GetInstance();

        Image2D* depthImage = target->GetDepthAttachmentImage().get();

        // Present the DEPTH attachment to a compute sampler and hand it back afterwards — its tracked
        // layout is not legal for a combined image sampler, and SetInput binds the tracked layout
        // verbatim.
        renderer.ComputeImageBeginRead( depthImage );
        renderer.ComputeImageBeginRead( m_NoiseVolume );
        renderer.ComputeImageBeginRead( m_ProfileTable.get() );
        renderer.ComputeImageBeginWrite( m_TraceImage.get() );
        renderer.ComputeImageBeginWrite( m_TraceGuideImage.get() );

        m_MarchPipeline->SetOutput( kCloudOutputBinding, m_TraceImage.get(), 0 );
        m_MarchPipeline->SetOutput( kCloudGuideOutputBinding, m_TraceGuideImage.get(), 0 );
        m_MarchPipeline->SetStorageBuffer( kCloudParamsBinding, m_ParamsBuffer.get() );
        m_MarchPipeline->SetInput( kCloudSceneDepthBinding, depthImage );
        m_MarchPipeline->SetInput( kCloudNoiseBinding, m_NoiseVolume );
        m_MarchPipeline->SetInput( kCloudProfileBinding, m_ProfileTable.get() );

        // ALWAYS bound, even when the payload's gate says it will not be read: a declared sampler with no
        // image is an invalid descriptor set, not an unused one, and this backend answers an invalid set
        // by skipping the whole dispatch — the clouds would vanish with nothing in the log.
        m_MarchPipeline->SetInput(
             kCloudDistantSkyLightBinding,
             atmosphere.DistantSkyLight
                  ? atmosphere.DistantSkyLight
                  : FallbackTextures::Get().GetFallbackTexture2D( Core::Formats::ImageFormat::RGBA8F ).get() );

        // The aerial-perspective volume, on the same terms: always bound, read only when the payload's
        // gate says the volume is real. It is what makes a cloud at the horizon the colour of the sky
        // instead of an opaque white wall.
        m_MarchPipeline->SetInput(
             kCloudAerialPerspectiveBinding,
             atmosphere.AerialPerspectiveVolume
                  ? atmosphere.AerialPerspectiveVolume
                  : FallbackTextures::Get().GetFallbackTexture3D( Core::Formats::ImageFormat::RGBA8F ).get() );

        m_MarchPipeline->SetPushConstants( &push, static_cast<uint32_t>( sizeof( push ) ) );

        const uint32_t traceWidth  = HalfExtent( m_HalfWidth );
        const uint32_t traceHeight = HalfExtent( m_HalfHeight );

        renderer.DispatchComputeInFrame( m_MarchPipeline.get(), GroupCount( traceWidth, kMarchWorkGroupSize ),
                                         GroupCount( traceHeight, kMarchWorkGroupSize ), 1 );

        renderer.ComputeImageEndWrite( m_TraceGuideImage.get() );
        renderer.ComputeImageEndWrite( m_TraceImage.get() );
        renderer.ComputeImageEndRead( m_ProfileTable.get() );
        renderer.ComputeImageEndRead( m_NoiseVolume );
        renderer.ComputeImageEndRead( depthImage );

        // S2 — THE TEMPORAL RECONSTRUCTION. The slot written alternates with the frame index, so the one
        // written last frame is still intact to be read. Both are real allocations from the first frame
        // onwards; what changes is whether their CONTENT means anything, and that is m_HistoryValid.
        const uint32_t writeIndex = m_FrameIndex & 1u;
        const uint32_t readIndex  = 1u - writeIndex;

        CloudResolveParams resolve{};
        resolve.InverseViewProjection = push.InverseViewProjection;
        resolve.PrevViewProjection    = m_PrevViewProjection;
        resolve.CameraPosition        = camera->GetPosition();
        resolve.HistoryValid          = m_HistoryValid ? 1.0f : 0.0f;
        resolve.SubPixelOffset =
             glm::ivec2( static_cast<int32_t>( subPixel.X ), static_cast<int32_t>( subPixel.Y ) );
        m_ResolveParamsBuffer->SetData( &resolve, static_cast<uint32_t>( sizeof( resolve ) ) );

        renderer.ComputeImageBeginWrite( m_HistoryImage[writeIndex].get() );
        renderer.ComputeImageBeginWrite( m_HistoryGuideImage[writeIndex].get() );

        m_ResolvePipeline->SetOutput( kCloudResolveOutputBinding, m_HistoryImage[writeIndex].get(), 0 );
        m_ResolvePipeline->SetOutput( kCloudResolveGuideOutputBinding, m_HistoryGuideImage[writeIndex].get(), 0 );
        m_ResolvePipeline->SetStorageBuffer( kCloudResolveParamsBinding, m_ResolveParamsBuffer.get() );
        m_ResolvePipeline->SetInput( kCloudResolveTraceBinding, m_TraceImage.get() );
        m_ResolvePipeline->SetInput( kCloudResolveTraceGuideBinding, m_TraceGuideImage.get() );

        // THE HISTORY, OR SOMETHING REAL IN ITS PLACE. Before the first reconstruction the read slot has
        // never been written, so its device memory is uninitialised AND its tracked layout is the one it
        // was created in — binding it would be an invalid descriptor, and this backend answers an invalid
        // set by skipping the whole dispatch, which loses the clouds with nothing in the log. The engine's
        // fallback texture is bound instead and CloudResolveParams::HistoryValid tells the shader to
        // ignore it. RGBA8F because FallbackTextures only provides RGBA8F and RGBA32F, and the sampler
        // reads floats either way.
        Image2D* historyScatter = m_HistoryValid ? m_HistoryImage[readIndex].get() : nullptr;
        Image2D* historyGuide   = m_HistoryValid ? m_HistoryGuideImage[readIndex].get() : nullptr;
        Image2D* fallback =
             FallbackTextures::Get().GetFallbackTexture2D( Core::Formats::ImageFormat::RGBA8F ).get();

        m_ResolvePipeline->SetInput( kCloudResolveHistoryBinding, historyScatter ? historyScatter : fallback );
        m_ResolvePipeline->SetInput( kCloudResolveHistoryGuideBinding, historyGuide ? historyGuide : fallback );

        renderer.DispatchComputeInFrame( m_ResolvePipeline.get(), GroupCount( m_HalfWidth, kMarchWorkGroupSize ),
                                         GroupCount( m_HalfHeight, kMarchWorkGroupSize ), 1 );

        renderer.ComputeImageEndWrite( m_HistoryGuideImage[writeIndex].get() );
        renderer.ComputeImageEndWrite( m_HistoryImage[writeIndex].get() );

        // Recorded AFTER the dispatch that used the previous value, so the matrix always describes the
        // frame whose pixels are now in the history rather than the frame being drawn.
        m_PrevViewProjection = viewProjection;
        m_ResolvedIndex      = writeIndex;
        m_HistoryValid       = true;

        ++m_FrameIndex;
        m_HasFrameResult = true;
    }

    void VolumetricCloudRenderer::RegisterPasses( RenderGraphBuilder& builder )
    {
        const auto target = m_TargetFramebuffer.lock();
        if ( !target || !m_CompositePipeline )
            return;

        RenderGraphBuilder::PassConfig config;
        config.Name        = "CloudComposite";
        config.Phase       = RenderPhase::Transparency;
        config.ExecuteFunc = [this]()
        {
            // The RECONSTRUCTION, not the trace: the composite upsamples half to full, and the half-res
            // pair is what the resolve wrote this frame.
            if ( !m_HasFrameResult || !m_HistoryImage[m_ResolvedIndex] || !m_HistoryGuideImage[m_ResolvedIndex] ||
                 !m_CompositeMaterial )
                return;

            m_CompositeMaterial->Bind( m_HistoryImage[m_ResolvedIndex].get(),
                                       m_HistoryGuideImage[m_ResolvedIndex].get() );
            Renderer::GetInstance().SubmitFullscreenQuad( m_CompositePipeline.get(),
                                                          m_CompositeMaterial->GetMaterialExecutor() );
        };
        config.PipelineSpec      = m_CompositePipeline->GetSpecification();
        config.TargetFramebuffer = target;
        config.Dependencies      = { RenderPassDependency( RenderPhase::Geometry ) };

        // FarField: above the atmospheric fog, below everything else the Transparency phase composites.
        // Stated here, on the pass itself, rather than implied by the order of the RegisterSystem calls —
        // that ordering is a tie-break, not a contract, and it moves when an unrelated system is added.
        config.OrderInPhase = RenderPassOrder::FarField;

        builder.AddPass( config );
    }
} // namespace Desert::Graphic::System
