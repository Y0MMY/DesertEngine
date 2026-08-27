#include "VolumetricCloudRenderer.hpp"

#include <Engine/Core/Camera.hpp>
#include <Engine/Graphic/FallbackTextures.hpp>
#include <Engine/Graphic/RenderGraphSort.hpp>
#include <Engine/Graphic/RenderPhase.hpp>
#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <Common/Core/Logger.hpp>
#include <Common/Core/Profiler.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>

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
        constexpr const char* kShadowMapShaderName = "CloudShadowMap";

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
        //
        // THE TWO APPLICATIONS ARE NOT A DOUBLE-APPLICATION, and the difference has now cost one
        // measurement, so it is written down. The call at ExecuteInFrame produces the HALF extent — the
        // reconstruction the composite upsamples from. EnsureTraceTargets applies it again to produce the
        // QUARTER extent — the trace the march writes. They are two targets at two sizes, both bound at
        // their own size, and the march is told the half size explicitly through CloudPush::Trace.zw
        // because imageSize() reports only the quarter one. Removing either call does not "undo a
        // doubling"; it moves the whole pyramid up an octave.
        //
        // AND THAT OCTAVE WAS MEASURED AND REFUSED. Р0 (Docs/Clouds/DIAGNOSIS_CARTOON.md §4.5, §8) found
        // that over cloud pixels our fine-scale energy is about half the UE reference's and named the
        // quarter-resolution trace as the last un-eliminated suspect. Р6 raised the pyramid one octave —
        // trace at half, reconstruction at native, so EVERY displayed pixel gets its own traced ray and
        // no resolution deficit remains — and shot the six protocol points at 90 and 3 frames on a
        // measured zero noise floor (Clouds_Protocol, 1280x766, --play, Debug/MoltenVK):
        //
        //     E1 over cloud pixels    quarter (shipped)   half     UE reference
        //     zenith away, 42 deg          0.00165       0.00167      0.00318
        //     mid away, 24 deg             0.00198       0.00204      0.00375
        //
        // +1.2 % and +3.0 % of a quantity short by ~48 %, contrast unchanged to 0.001 at all six points,
        // and the frames are indistinguishable except for a marginal crispening of the far-field band at
        // 7 deg. The price, per-pass GPU self time, minimum of six interleaved runs on a SHARED machine:
        // Clouds: March 12.695 -> 35.907 ms (2.83x, +23.2 ms); pass memory 8.42 -> 33.66 MiB, which at
        // 1920x1080 is 71.2 MiB and exceeds decision D-9's whole 64 MB subsystem budget on its own.
        //
        // So the quarter is a budget and not a defect, and the fine-scale deficit is NOT resolution — a
        // native-resolution march does not close it. What limits the surface is still open; §4.5's
        // signature (our frames surviving a 4x round trip better than the reference) barely moves at
        // native resolution too, 58.7 -> 58.2 % and 60.8 -> 59.4 % against the reference's 45.7 / 53.8 %,
        // so that statistic was reading the smoothness of the cloud itself, not the sampling grid.
        // What would change the answer: a subject-matched reference (§8's near cumulus deck), or a
        // mechanism that adds surface rather than sampling it more finely.
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

        // The shadow map's own copy of the same block, on the same non-persistent terms. Two buffers
        // because two dispatches on opposite sides of the render graph read them, not because there are
        // two sets of numbers — see the member's comment.
        m_ShadowParamsBuffer = ShaderResources::StorageBuffer::Create(
             "CloudShadowParams", kCloudPayloadBytes, kCloudShadowParamsBinding, /*persistent=*/false );
        if ( !m_ShadowParamsBuffer )
            return Common::MakeError(
                 "VolumetricCloudRenderer: could not create the cloud shadow parameter buffer" );

        // Slot A's instance list, doubled for the same reason the parameter block is: two dispatches on
        // opposite sides of the render graph read it. Non-persistent, so the backend keeps one copy per
        // (frame x recording renderer slot) — the Docs/RENDERER_FRAME_STATE.md rule.
        //
        // ALLOCATED UNCONDITIONALLY, 656 bytes each, and that is the one place this feature costs a scene
        // that does not use it. A buffer created lazily would have to be created inside the dispatch path,
        // where a failure has nowhere to go but a silent skip — and the descriptor has to exist anyway,
        // because a declared storage block with no buffer is the same invalid descriptor set a missing
        // sampler is.
        m_AuthoredBuffer = ShaderResources::StorageBuffer::Create( "CloudAuthored", kCloudAuthoredPayloadBytes,
                                                                   kCloudAuthoredBinding, /*persistent=*/false );
        if ( !m_AuthoredBuffer )
            return Common::MakeError( "VolumetricCloudRenderer: could not create the hero cloud instance buffer" );

        m_ShadowAuthoredBuffer =
             ShaderResources::StorageBuffer::Create( "CloudShadowAuthored", kCloudAuthoredPayloadBytes,
                                                     kCloudShadowAuthoredBinding, /*persistent=*/false );
        if ( !m_ShadowAuthoredBuffer )
            return Common::MakeError(
                 "VolumetricCloudRenderer: could not create the hero cloud instance buffer for the shadow map" );

        m_CompositeMaterial = std::make_unique<MaterialCloudComposite>();
        return BOOLSUCCESS;
    }

    void VolumetricCloudRenderer::Shutdown()
    {
        m_MarchPipeline.reset();
        m_ResolvePipeline.reset();
        m_ShadowMapPipeline.reset();
        m_CompositePipeline.reset();
        m_CompositeMaterial.reset();
        m_TraceImage.reset();
        m_TraceGuideImage.reset();
        m_ShadowMapImage.reset();
        m_ShadowMapValid = false;
        for ( uint32_t i = 0; i < 2u; ++i )
        {
            m_HistoryImage[i].reset();
            m_HistoryGuideImage[i].reset();
        }
        m_ModellingVolume.reset();
        m_ModellingValid = false;
        for ( uint32_t slot = 0; slot < kCloudSpeciesSlots; ++slot )
            m_ProfileTypes[slot] = Assets::AssetHandle::Null();
        m_ProfileSpeciesCount = 0;
        m_ProfileGeneration   = 0;
        m_ParamsBuffer.reset();
        m_ResolveParamsBuffer.reset();
        m_ShadowParamsBuffer.reset();
        m_AuthoredBuffer.reset();
        m_ShadowAuthoredBuffer.reset();
        m_HeroClouds.clear();
        m_AuthoredPayload = CloudAuthoredPayload{};
        m_AuthoredAtlas   = nullptr;
    }

    uint32_t VolumetricCloudRenderer::ResolveSpecies( CloudTypeShape ( &shapes )[kCloudSpeciesSlots],
                                                      Assets::AssetHandle ( &handles )[kCloudSpeciesSlots] ) const
    {
        // The component has four type fields and the payload has four species channels, and this is the
        // one place both are indexed by the same number. If they ever part, every array below overflows
        // by exactly as much as they differ.
        static_assert( ECS::kCloudTypeSlots == kCloudSpeciesSlots,
                       "a layer's cloud type slots and the payload's species slots are indexed together" );

        auto* types = Runtime::ResourceRegistry::GetCloudTypeService();

        // WHICH SLOTS BECOME SPECIES IS STATED ONCE, IN ECS::ResolveCloudSpecies, and it stopped being the
        // renderer's private business the moment a painted layout indexed its channels by SPECIES: the
        // Cloud Layout panel has to name the type behind channel 2, and a panel that compacted the slots
        // for itself would name a different one the day this rule moved. Empty slots are skipped and a
        // repeated type is dropped there, for the reason given there.
        const ECS::CloudSpeciesResolution resolved = ECS::ResolveCloudSpecies( m_Data );

        if ( resolved.BuiltInDefault )
        {
            // ALL FOUR EMPTY IS A DOCUMENTED ANSWER, and it is the SAME answer an empty single slot gave
            // before there were four: one built-in cumulus congestus. A scene nobody has authored a type
            // for still has to have a sky.
            handles[0] = Assets::AssetHandle::Null();
            shapes[0]  = Assets::CloudTypeDefaultShape();
            return 1;
        }

        const Assets::AssetHandle authored[ECS::kCloudTypeSlots] = { m_Data.CloudType1, m_Data.CloudType2,
                                                                     m_Data.CloudType3, m_Data.CloudType4 };

        for ( uint32_t species = 0; species < resolved.Count; ++species )
        {
            handles[species] = authored[resolved.AuthoredSlot[species]];
            shapes[species]  = types->GetShape( handles[species] );
        }

        return resolved.Count;
    }

    Assets::CloudProceduralFieldParams
    VolumetricCloudRenderer::BuildProceduralParams( const CloudTypeShape* shapes, uint32_t speciesCount ) const
    {
        Assets::CloudProceduralFieldParams params;

        params.RegionSizeKm = std::max( m_Data.RegionSize, 1.0f ) / kCloudWorldUnitsPerKm;

        // THE SHELL, TAKEN FROM THE SPECIES AND NOT FROM THE COMPONENT, because that is where the packer
        // takes it from too: the layer's geometry is the UNION of its types' altitude ranges (decision
        // D-13's envelope), and a volume spread over a different shell than the one the march intersects
        // would put every cloud at the wrong altitude — the "sky was a ceiling" defect in a new costume.
        const CloudEnvelopeKm envelope = CloudTypeSetEnvelopeKm( shapes, speciesCount );

        params.LayerBottomKm    = std::max( envelope.BottomKm, 0.0f );
        params.LayerThicknessKm = std::max( envelope.TopKm - params.LayerBottomKm, 0.001f );

        params.Coverage         = std::clamp( m_Data.Coverage, 0.0f, 1.0f );
        params.CoverageContrast = std::max( m_Data.CoverageContrast, 0.01f );
        params.Seed             = static_cast<uint32_t>( m_Data.Seed );

        // THE FOUR PLACEMENT NUMBERS PASS THROUGH UNCHANGED, and the clamps here are the component's own
        // ranges rather than second opinions: a scene file is a text file and an out-of-range number in
        // one must produce a sky rather than a refusal. Assets::ValidateCloudProceduralParams refuses
        // anything outside them by name, so a clamp that disagreed with a range would turn an artist's
        // typo into a layer that never bakes.
        params.PlacementDensity     = std::clamp( m_Data.PlacementDensity, 0.25f, 8.0f );
        params.PlacementScatter     = std::clamp( m_Data.PlacementScatter, 0.0f, 4.0f );
        params.PlacementSizeVariety = std::clamp( m_Data.PlacementSizeVariety, 0.0f, 1.0f );
        params.PatchStrength        = std::clamp( m_Data.PatchStrength, 0.0f, 1.0f );

        // THE PATCH IS THE ONE THAT CAN REFUSE, because it is half of a RELATION — a modulation finer than
        // three cells decides cells one at a time and reads as a checkerboard. Floored against the
        // lattice HERE rather than left to fail validation, for the same reason: the layer has to draw a
        // sky for whatever the file says. An artist who wants finer patches gets them by shrinking the
        // weather tile, which is what the tooltip names.
        params.PatchTileKm = std::max( m_Data.PatchTileSize, 1.0f ) / kCloudWorldUnitsPerKm;

        // THE BLEND RADIUS AND THE PROFILE DEPTH ARE DERIVED FROM THE LATTICE rather than exposed, and
        // that is a decision with a number behind it. The join inflates its own surface by
        // `BlendRadius * ln(sum of weights in range)`, so with hundreds of overlapping lumps a generous
        // radius does not soften a crease, it floods the sky — at a 3 km cell and 24 lumps in range, a
        // radius of a fifth of the cell would dilate every body by 1.9 km. Two per cent of the cell keeps
        // that dilation under 200 m while still fusing lobes that already overlap, which is where the
        // fusion comes from. An artist who wants softer clouds has Detail Strength, which is the knob that
        // means it.
        // ONE STATEMENT OF "four cells to a tile", shared with the Cloud Layout panel, which measures a
        // painting's strokes against the cell and must not compute the ratio a second time.
        const float latticeKm = ECS::CloudLayerLatticeKm( m_Data );

        params.BlendRadiusKm  = std::max( 0.02f * latticeKm, 1e-3f );
        params.ProfileDepthKm = std::max( 0.12f * latticeKm, 1e-3f );

        // THE MARCH'S OWN SEARCH STEP, handed in rather than assumed by the generator. It is one half of
        // the relation this programme has been bitten by twice — what the field places against what the
        // ray can find — and taking it from the component's Max Steps is what makes an artist who lowers
        // that number get coarser lumps rather than speckle.
        params.ResolvableChordKm =
             CloudFinestResolvableChordKm( static_cast<float>( std::clamp( m_Data.MaxSteps, 8, 512 ) ) );

        const glm::vec3 wind = m_Data.WindDirection;
        params.WindAxis      = glm::vec2( wind.x, wind.z );

        params.Species.reserve( speciesCount );
        for ( uint32_t slot = 0; slot < speciesCount; ++slot )
        {
            Assets::CloudProceduralSpecies species;
            species.Shape = shapes[slot];
            // A TYPE STATES HOW MUCH COARSER OR FINER THAN THE LAYER IT IS, which is what Placement Scale
            // has always meant, and the layer's own tile is the pair Max View Distance is calibrated
            // against (CALIBRATION.md §4). Four cells to a tile, which is the ratio the component's own
            // tooltip has stated since T1: "12 km -> 3 km cells, a cumulus field".
            species.CellKm     = latticeKm * std::max( shapes[slot].PlacementScale, 1e-3f );
            species.Anisotropy = std::max( shapes[slot].PlacementAnisotropy, 1e-3f );
            params.Species.push_back( species );
        }

        // THE PATCH AGAINST THE LATTICE, floored after the species are known because the CELL is what the
        // relation is against and a type's Placement Scale and Anisotropy both move it. Three cells is the
        // bound Assets::ValidateCloudProceduralParams refuses below: a modulation whose period is near a
        // cell's decides cells one at a time, which is a checkerboard and not a weather system.
        for ( const Assets::CloudProceduralSpecies& species : params.Species )
        {
            const glm::vec2 extent = Assets::CloudProceduralCellExtentKm( params, species );
            params.PatchTileKm     = std::max( params.PatchTileKm, 3.0f * std::max( extent.x, extent.y ) );
        }

        // THE PAINTED LAYOUT. Resolved through the service exactly as the cloud types above are, so the
        // renderer never learns how to read a file and three viewports resolve one asset once.
        //
        // THE CLAMPS ARE THE COMPONENT'S OWN RANGES rather than second opinions, for the reason the four
        // placement numbers state above them: a scene file is a text file, an out-of-range number in one
        // must produce a sky rather than a refusal, and Assets::ValidateCloudLayoutPlacement refuses
        // anything outside them by name — so a clamp that disagreed with a range would turn a typo into a
        // layer that never bakes.
        params.LayoutPlacement.RepeatsPerRegion =
             static_cast<uint32_t>( std::clamp( m_Data.LayoutRepeats, 1, 16 ) );
        params.LayoutPlacement.QuarterTurns = static_cast<uint32_t>( std::clamp( m_Data.LayoutRotation, 0, 3 ) );
        params.LayoutPlacement.OffsetKm =
             glm::vec2( m_Data.LayoutOffset.x, m_Data.LayoutOffset.y ) / kCloudWorldUnitsPerKm;
        params.LayoutPlacement.PatternStrength = std::clamp( m_Data.LayoutPatternStrength, 0.0f, 1.0f );
        params.LayoutPlacement.MaskStrength    = std::clamp( m_Data.LayoutMaskStrength, 0.0f, 1.0f );

        // A NULL HERE IS THE SHIPPED STATE AND NOT A FAILURE — every scene in this repository carries an
        // empty slot, and the bake reads null as "there is no painting" and places the sky exactly as it
        // did before this field existed. A handle that names a layout nobody registered is logged by the
        // service, once, and also arrives here as null.
        params.Layout = Runtime::ResourceRegistry::GetCloudLayoutService()->Get( m_Data.CloudLayout );

        // WHEN THE PAINTING CANNOT BE HONOURED IT IS DROPPED, NOT THE SKY. The narrow validator is the one
        // called here on purpose — handed the whole one, a mistyped patch tile would have dropped the
        // artist's painting and blamed the painting for it (see ValidateCloudProceduralLayout).
        //
        // Said out loud, because a painting that silently stopped applying is the least diagnosable thing
        // this slot can do — and said ONCE per painting rather than once per frame, because this function
        // runs every frame and a message at sixty hertz is a log nobody reads.
        if ( params.Layout )
        {
            if ( const auto usable = Assets::ValidateCloudProceduralLayout( params ); !usable )
            {
                if ( m_ReportedBadLayoutHash != params.Layout->ContentHash )
                {
                    m_ReportedBadLayoutHash = params.Layout->ContentHash;
                    LOG_ERROR( "[Clouds] The bound cloud layout is not usable and the sky will be placed "
                               "procedurally instead: {}",
                               usable.GetError() );
                }
                params.Layout = nullptr;
            }
            else
            {
                m_ReportedBadLayoutHash = 0u;
            }
        }

        return params;
    }

    bool VolumetricCloudRenderer::EnsureModellingVolume()
    {
        auto* types = Runtime::ResourceRegistry::GetCloudTypeService();

        CloudTypeShape      shapes[kCloudSpeciesSlots]{};
        Assets::AssetHandle handles[kCloudSpeciesSlots]{};
        const uint32_t      speciesCount = ResolveSpecies( shapes, handles );

        const uint32_t generation = types->GetGeneration();

        const auto* camera = m_SceneRenderer->GetMainCamera();
        if ( !camera )
            return false;

        const Assets::CloudProceduralFieldParams wanted = BuildProceduralParams( shapes, speciesCount );

        // WHERE THE REGION WANTS TO BE. The camera MINUS the accumulated wind, because the march asks the
        // volume about `position - wind`: the sky drifting downwind for an hour must not carry the region
        // away from the camera that is looking at it.
        const glm::vec3 cameraKm = camera->GetPosition() / kCloudWorldUnitsPerKm;
        const glm::vec3 windKm   = m_WindOffset / kCloudWorldUnitsPerKm;

        const glm::vec2 wantedOrigin =
             Assets::CloudProceduralRegionOriginKm( wanted, cameraKm.x - windKm.x, cameraKm.z - windKm.z );

        // ---------------------------------------------------------------------------------------------
        // START ONE IF WHAT IS ON THE DEVICE IS NOT WHAT IS WANTED
        // ---------------------------------------------------------------------------------------------
        if ( !m_ModellingBake.valid() && !m_ModellingFailed )
        {
            const bool sameSet =
                 m_ModellingValid && m_ProfileSpeciesCount == speciesCount && m_ProfileGeneration == generation;

            bool sameTypes = sameSet;
            for ( uint32_t slot = 0; sameTypes && slot < speciesCount; ++slot )
                sameTypes = m_ProfileTypes[slot] == handles[slot];

            const bool sameRegion = m_ModellingValid && m_ModellingOriginKm == wantedOrigin;
            const bool sameParams =
                 m_ModellingValid && Assets::CloudProceduralParamsEqual( m_ModellingParams, wanted );

            if ( !( sameTypes && sameRegion && sameParams ) )
            {
                if ( auto valid = Assets::ValidateCloudProceduralParams( wanted ); !valid )
                {
                    m_ModellingFailed = true;
                    LOG_ERROR( "[Clouds] The cloud layer cannot be turned into a modelling volume: {}",
                               valid.GetError() );
                    return m_ModellingValid;
                }

                m_PendingParams   = wanted;
                m_PendingOriginKm = wantedOrigin;

                // ON A WORKER, and the frame does not wait for it. See the note on the declaration: the
                // bake is measured at hundreds of milliseconds to seconds in a Debug build, and a region
                // shift happens once per lattice cell of camera travel.
                m_ModellingBakeStarted = std::chrono::steady_clock::now();

                m_ModellingBake = std::async( std::launch::async, [params = wanted, origin = wantedOrigin]()
                                              { return Assets::BakeCloudProceduralVolume( params, origin ); } );
            }
        }

        // ---------------------------------------------------------------------------------------------
        // COLLECT A FINISHED BAKE
        // ---------------------------------------------------------------------------------------------
        // THE FIRST BAKE OF A SCENE BLOCKS, AND EVERY LATER ONE DOES NOT. The difference is whether there
        // is anything to march meanwhile.
        //
        // A REBAKE has a previous volume: the camera has left the region it was baked for by at most one
        // snap step, the volume is periodic, so the frame reads the neighbouring tile — the same
        // degenerate far path the sky past the region already uses — and waiting for a better answer would
        // be a hitch in exchange for nothing anybody can see.
        //
        // THE FIRST BAKE has none, and the consequence of not waiting was measured rather than reasoned
        // about: with the pass skipped the frames cost almost nothing, so a headless shot of ninety frames
        // finished BEFORE an 800 ms bake did and wrote an empty sky. In an editor it is the same defect
        // with a friendlier face — the sky appears a second after the scene does, and the temporal resolve
        // then takes ten more frames to converge it.
        if ( m_ModellingBake.valid() && !m_ModellingValid )
            m_ModellingBake.wait();

        if ( m_ModellingBake.valid() &&
             m_ModellingBake.wait_for( std::chrono::seconds( 0 ) ) == std::future_status::ready )
        {
            const auto baked = m_ModellingBake.get();

            if ( !baked )
            {
                m_ModellingFailed = true;
                LOG_ERROR( "[Clouds] The procedural modelling volume could not be baked: {}", baked.GetError() );
                return m_ModellingValid;
            }

            // A FRESH IMAGE RATHER THAN AN IN-PLACE UPLOAD, which is the choice CloudNoiseService makes
            // and for the same reason: SetData writes into an image that frames still in flight may be
            // sampling. A rebake happens once per lattice cell of camera travel, so the allocation is not
            // worth the synchronisation argument. The old image goes through Image3D's deletion queue
            // when the last reference drops.
            if ( m_ModellingVolume )
                Renderer::GetInstance().WaitDeviceIdle();

            const Core::Formats::Image3DSpecification spec{
                 .Tag        = "CloudModellingVolume",
                 .Width      = Assets::kCloudProceduralVolumeWidth,
                 .Height     = Assets::kCloudProceduralVolumeHeight,
                 .Depth      = Assets::kCloudProceduralVolumeDepth,
                 .Format     = Core::Formats::ImageFormat::RGBA8F,
                 .Data       = baked.GetValue(),
                 .Properties = Core::Formats::Sample,
            };

            m_ModellingVolume = Image3D::Create( spec );
            if ( !m_ModellingVolume )
            {
                m_ModellingFailed = true;
                LOG_ERROR( "[Clouds] The {}x{}x{} RGBA8 procedural modelling volume could not be created on "
                           "the device; the clouds will not render for this view.",
                           Assets::kCloudProceduralVolumeWidth, Assets::kCloudProceduralVolumeHeight,
                           Assets::kCloudProceduralVolumeDepth );
                m_ModellingValid = false;
                return false;
            }

            m_ModellingParams   = m_PendingParams;
            m_ModellingOriginKm = m_PendingOriginKm;
            m_ModellingValid    = true;

            for ( uint32_t slot = 0; slot < kCloudSpeciesSlots; ++slot )
                m_ProfileTypes[slot] = slot < speciesCount ? handles[slot] : Assets::AssetHandle::Null();
            m_ProfileSpeciesCount = speciesCount;
            m_ProfileGeneration   = generation;

            // THE COST OF A REBAKE IS PRINTED, NOT ASSUMED, and that is the exit criterion this phase was
            // given (ANALYSIS_APPROACH.md §3). It is wall time from starting the worker to collecting it,
            // so it includes whatever else the machine was doing — which is the number that matters,
            // because what it bounds is how far the sky lags the camera.
            const double bakeMs = std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() -
                                                                             m_ModellingBakeStarted )
                                       .count();

            LOG_INFO( "[Clouds] Modelling volume baked for {} cloud type(s) in {:.0f} ms — region {:.0f} km "
                      "at ({:.1f}, {:.1f}), envelope {:.2f} to {:.2f} km, {}x{}x{} RGBA8 ({:.2f} MiB), "
                      "{} lumps, {:.0f} m per voxel.",
                      speciesCount, bakeMs, m_ModellingParams.RegionSizeKm, m_ModellingOriginKm.x,
                      m_ModellingOriginKm.y, m_ModellingParams.LayerBottomKm,
                      m_ModellingParams.LayerBottomKm + m_ModellingParams.LayerThicknessKm,
                      Assets::kCloudProceduralVolumeWidth, Assets::kCloudProceduralVolumeHeight,
                      Assets::kCloudProceduralVolumeDepth,
                      BytesToMiB( static_cast<size_t>( Assets::kCloudProceduralVoxelBytes ) ),
                      Assets::CountCloudProceduralBlobs( m_ModellingParams, m_ModellingOriginKm ),
                      m_ModellingParams.RegionSizeKm / static_cast<float>( Assets::kCloudProceduralVolumeWidth ) *
                           1000.0f );
        }

        return m_ModellingValid;
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

        const auto shadowShader = shaderService->GetByName( kShadowMapShaderName );
        if ( !shadowShader )
        {
            LOG_ERROR( "[Clouds] Compute shader '{}' is not registered. Expected "
                       "Editor/Resources/Shaders/Programs/Clouds/{}.shader.",
                       kShadowMapShaderName, kShadowMapShaderName );
            return false;
        }
        m_ShadowMapPipeline =
             ComputePipeline::Create( { .Shader = shadowShader, .DebugName = kShadowMapShaderName } );
        if ( !m_ShadowMapPipeline )
            return false;
        m_ShadowMapPipeline->Invalidate();

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

        return m_MarchPipeline && m_ResolvePipeline && m_ShadowMapPipeline && m_CompositePipeline;
    }

    bool VolumetricCloudRenderer::EnsureShadowMap( uint32_t resolution )
    {
        if ( m_ShadowMapFailed )
            return false;

        const float scale = CloudQualityFor( m_Quality ).ShadowMapScale;
        if ( m_ShadowMapImage && m_ShadowMapScaleInUse == scale )
            return true;

        // ALLOCATED ON THE FIRST FRAME THE LAYER ACTUALLY CASTS, and reallocated only when the QUALITY
        // TIER changes its size — never when the viewport does. That distinction is the whole reason this
        // is not part of EnsureTraceTargets: the six trace targets ARE the view's size and a resize must
        // throw them away, while this map is a world-space quantity and a resize must leave it standing.
        //
        // The old image may still be referenced by descriptors of frames in flight, exactly as in
        // EnsureTraceTargets, so the device is idled before it is dropped. A tier switch is a rare,
        // human-initiated event; paying a full idle for it is the cheap answer and the safe one.
        if ( m_ShadowMapImage )
            Renderer::GetInstance().WaitDeviceIdle();

        const Core::Formats::Image2DSpecification spec{
             .Tag        = "CloudShadowMap",
             .Width      = resolution,
             .Height     = resolution,
             .Format     = Core::Formats::ImageFormat::RGBA32F,
             .Mips       = 1u,
             .Usage      = Core::Formats::Image2DUsage::Image2D,
             .Properties = Core::Formats::Storage | Core::Formats::Sample,
        };

        m_ShadowMapImage = Image2D::Create( spec, nullptr );
        if ( !m_ShadowMapImage )
        {
            LOG_ERROR( "[Clouds] The {}x{} RGBA32F cloud shadow map could not be created on the device; "
                       "the clouds will not shade the world for this view.",
                       resolution, resolution );
            m_ShadowMapFailed = true;
            return false;
        }

        m_ShadowMapScaleInUse = scale;

        const float extentKm = CloudShadowExtentKmForScale( scale );
        LOG_INFO( "[Clouds] Cloud shadow map {}x{} RGBA32F ({:.2f} MiB) — {:.0f} km across the world, "
                  "{:.1f} m per texel, snapped to a {:.0f} km grid, quality scale {:.2f}.",
                  resolution, resolution,
                  BytesToMiB( Core::Formats::CalculateImageSize( resolution, resolution,
                                                                 Core::Formats::ImageFormat::RGBA32F ) ),
                  2.0f * extentKm, 2.0f * extentKm * 1000.0f / static_cast<float>( resolution ),
                  CloudShadowSnapKmForScale( scale ), scale );
        return true;
    }

    float VolumetricCloudRenderer::GetShadowStrength() const
    {
        // ZERO WHEN THE LAYER IS NOT CASTING, so a consumer that reads only this number cannot light the
        // world through a map that was never written. It is the same gate ExecuteShadowMapInFrame uses,
        // stated once here rather than repeated at every call site.
        if ( !m_Present || !m_Data.Enabled || !m_Data.CastShadows )
            return 0.0f;
        return std::clamp( m_Data.ShadowStrength, 0.0f, 1.0f );
    }

    void VolumetricCloudRenderer::ExecuteShadowMapInFrame()
    {
        DESERT_PROFILE_PASS( "Clouds: ShadowMap" );

        // Cleared FIRST and set only at the end, so every early return below leaves consumers with "no
        // cloud shadow this frame" rather than with the projection of a frame the sun has since left.
        m_ShadowMapValid = false;

        if ( !m_ShadowMapPipeline || !m_ShadowParamsBuffer )
            return;

        // THE ZERO-COST LADDER, and it is the same one the march has, plus the two fields this task
        // added. A scene with no cloud component, with the clouds off, with casting off or with the
        // strength at zero dispatches nothing and — because the allocation is below this line and not
        // above it — allocates nothing either. The editor builds a SceneRenderer for every asset
        // thumbnail and every mesh preview, and none of them has a sky.
        if ( GetShadowStrength() <= 0.0f )
            return;

        const AtmosphereEnv& atmosphere = m_SceneRenderer->GetAtmosphere();
        if ( !atmosphere.Valid )
            return;

        const auto* camera = m_SceneRenderer->GetMainCamera();
        if ( !camera )
            return;

        // THE TIER'S THREE NUMBERS, DERIVED FROM ONE SCALE. Resolution, extent and snap move together so
        // that the texel — and therefore the relation against the chord the view march can find, which
        // Desert/Tests/Engine/CloudShadow asserts for every tier — does not move at all. What the cheaper
        // tier buys is a smaller square of world, not a coarser one.
        const CloudQualityScale quality    = CloudQualityFor( m_Quality );
        const uint32_t          resolution = CloudShadowResolutionForScale( quality.ShadowMapScale );
        const float             extentKm   = CloudShadowExtentKmForScale( quality.ShadowMapScale );
        const float             snapKm     = CloudShadowSnapKmForScale( quality.ShadowMapScale );

        if ( !EnsureShadowMap( resolution ) )
            return;

        // THE SPECIES COME FIRST NOW, and the order is load-bearing rather than tidy: the volumes a layer
        // binds are named by its TYPES, so the set has to be resolved before there is anything to look
        // them up with.
        CloudTypeShape      shapes[kCloudSpeciesSlots]{};
        Assets::AssetHandle handles[kCloudSpeciesSlots]{};
        const uint32_t      speciesCount = ResolveSpecies( shapes, handles );

        if ( !EnsureNoiseVolumes( handles, speciesCount ) )
            return;
        if ( !EnsureModellingVolume() )
            return;

        const CloudGpuPayload payload =
             PackCloudParams( m_Data, shapes, speciesCount, atmosphere, m_WindOffset,
                              CloudRegionBinding{ m_ModellingOriginKm, m_ModellingParams.RegionSizeKm },
                              quality.LightMarchSampleCeiling, quality.StopTransmittanceFloor, m_NoiseSlots );
        m_ShadowParamsBuffer->SetData( &payload, static_cast<uint32_t>( sizeof( payload ) ) );

        // Slot A, for the shadow map as well as for the eye: a hero cloud shades the ground under it
        // because it IS the cloud field, not because anything was added to the deferred pass.
        BuildAuthoredPayload( payload );
        m_ShadowAuthoredBuffer->SetData( &m_AuthoredPayload,
                                         static_cast<uint32_t>( sizeof( m_AuthoredPayload ) ) );

        // THE PLANET RADIUS IS TAKEN FROM THE PACKED BLOCK, not from the component, because the packer is
        // where it is floored — and a map centred on a different sphere than the one the march intersects
        // is a shadow displaced by the curvature.
        m_ShadowMapView = CloudBuildShadowMapView( camera->GetPosition(), atmosphere.SunDirection, payload.Layer.x,
                                                   extentKm, snapKm, static_cast<float>( resolution ) );

        CloudShadowPush push{};
        push.MapToWorld = m_ShadowMapView.MapToWorld;
        push.Trace      = glm::vec4( m_ShadowMapView.LightDirection, m_ShadowMapView.SampleCount );
        // The far plane the producer encodes its depths against, taken from the projection it is handed
        // rather than compiled from a constant — see Graphic::CloudShadowPush.
        push.Depth = glm::vec4( m_ShadowMapView.FarDepthKm, 0.0f, 0.0f, 0.0f );

        auto& renderer = Renderer::GetInstance();

        // ONLY THE DISTINCT ONES ARE TRANSITIONED, and only they can be: the barrier is per image, and
        // asking for the same image four times is four barriers on one resource rather than a no-op. The
        // descriptors below are still written four times, because a descriptor is not a barrier.
        for ( uint32_t slot = 0; slot < m_NoiseNeeded; ++slot )
            renderer.ComputeImageBeginRead( m_NoiseVolume[slot] );
        renderer.ComputeImageBeginRead( m_ModellingVolume.get() );
        if ( m_AuthoredAtlas )
            renderer.ComputeImageBeginRead( m_AuthoredAtlas );
        renderer.ComputeImageBeginWrite( m_ShadowMapImage.get() );

        m_ShadowMapPipeline->SetOutput( kCloudShadowOutputBinding, m_ShadowMapImage.get(), 0 );
        m_ShadowMapPipeline->SetStorageBuffer( kCloudShadowParamsBinding, m_ShadowParamsBuffer.get() );
        for ( uint32_t slot = 0; slot < kCloudSpeciesSlots; ++slot )
            m_ShadowMapPipeline->SetInput( kCloudShadowNoiseBindings[slot], m_NoiseVolume[slot] );
        m_ShadowMapPipeline->SetInput( kCloudShadowModellingBinding, m_ModellingVolume.get() );
        m_ShadowMapPipeline->SetStorageBuffer( kCloudShadowAuthoredBinding, m_ShadowAuthoredBuffer.get() );
        // ALWAYS bound, fallback included — see the note at the march's own binding of it.
        m_ShadowMapPipeline->SetInput(
             kCloudShadowAuthoredAtlasBinding,
             m_AuthoredAtlas
                  ? m_AuthoredAtlas
                  : FallbackTextures::Get().GetFallbackTexture3D( Core::Formats::ImageFormat::RGBA8F ).get() );
        m_ShadowMapPipeline->SetPushConstants( &push, static_cast<uint32_t>( sizeof( push ) ) );

        renderer.DispatchComputeInFrame( m_ShadowMapPipeline.get(), GroupCount( resolution, kMarchWorkGroupSize ),
                                         GroupCount( resolution, kMarchWorkGroupSize ), 1 );

        renderer.ComputeImageEndWrite( m_ShadowMapImage.get() );
        if ( m_AuthoredAtlas )
            renderer.ComputeImageEndRead( m_AuthoredAtlas );
        renderer.ComputeImageEndRead( m_ModellingVolume.get() );
        for ( uint32_t slot = m_NoiseNeeded; slot-- > 0; )
            renderer.ComputeImageEndRead( m_NoiseVolume[slot] );

        m_ShadowMapValid = true;
    }

    void VolumetricCloudRenderer::SetCloudSettings( bool present, const ECS::VolumetricCloudData& data,
                                                    const glm::vec3& windOffset, Core::CloudQuality quality,
                                                    const std::vector<HeroCloudInstance>& heroClouds )
    {
        m_Present    = present;
        m_Data       = data;
        m_WindOffset = windOffset;
        m_Quality    = quality;

        // RE-ARM THE WARNINGS WHEN THE ARRANGEMENT CHANGES. A latch that is never released says a thing
        // once and then lies for the rest of the session: an artist who moves a body back inside the
        // layer and out again would hear nothing the second time. Comparing the count is the cheap half
        // of "the arrangement changed" and it is the half that matters — adding or removing a hero cloud
        // is what re-opens both questions.
        if ( heroClouds.size() != m_HeroClouds.size() )
        {
            m_AuthoredFitWarned = false;
            m_AuthoredCrowdWarned = false;
        }

        m_HeroClouds = heroClouds;
    }

    void VolumetricCloudRenderer::BuildAuthoredPayload( const CloudGpuPayload& payload )
    {
        m_AuthoredPayload = CloudAuthoredPayload{};
        m_AuthoredAtlas   = nullptr;

        if ( m_HeroClouds.empty() )
            return;

        auto* service = Runtime::ResourceRegistry::GetCloudModellingService();

        // WHICH BODIES THIS FRAME NEEDS, before a single instance is packed. The atlas has to exist before
        // an instance can say where in it to look, and TWO ENTITIES NAMING ONE `.dcmv` SHARE A SLAB —
        // which is what makes a scene of forty copies of one sculpted body cost 4.00 MiB and not 160.
        std::vector<Assets::AssetHandle> bodies;
        bodies.reserve( kCloudModellingAtlasMaxSlabs );

        for ( const HeroCloudInstance& hero : m_HeroClouds )
        {
            if ( !service->HasBody( hero.Data.Volume ) )
                continue; // already logged by the service, with the handle in the message

            if ( std::find( bodies.begin(), bodies.end(), hero.Data.Volume ) != bodies.end() )
                continue;

            if ( bodies.size() >= kCloudModellingAtlasMaxSlabs )
            {
                // NOT a silent drop, and the number that ran out is named: it is the ATLAS and not the
                // instance list, so the fix is fewer DIFFERENT bodies rather than fewer entities.
                if ( !m_AuthoredBodiesWarned )
                {
                    LOG_WARN( "[Clouds] Hero cloud '{}' names the {}th different modelling volume in this "
                              "scene and the atlas holds {}; it is not drawn. Instances that SHARE a .dcmv "
                              "are free — it is the number of DIFFERENT bodies that is capped, by the "
                              "4.00 MiB each of them costs.",
                              hero.Name, bodies.size() + 1, kCloudModellingAtlasMaxSlabs );
                    m_AuthoredBodiesWarned = true;
                }
                continue;
            }

            bodies.push_back( hero.Data.Volume );
        }

        if ( bodies.empty() )
            return;

        // A CANONICAL SLAB ORDER, so that the atlas is a function of WHICH bodies are live and not of the
        // order the ECS happened to hand them over in. The service rebuilds when the list changes, and a
        // list that reorders itself would rebuild — twelve megabytes of upload — on a frame where nothing
        // about the sky moved.
        //
        // MEASURED RATHER THAN FEARED: without this sort the engine's own log line already appeared
        // exactly ONCE in a 900-frame render, so EnTT's view order is stable in practice. Sorting makes
        // it stable by construction, which is a cheaper guarantee than the observation — at most eight
        // handles, once a frame.
        std::sort( bodies.begin(), bodies.end() );

        const Runtime::CloudModellingAtlasBinding atlas = service->EnsureAtlas( bodies );
        if ( !atlas.Volume )
            return; // already logged by the service

        m_AuthoredAtlas             = atlas.Volume;
        m_AuthoredPayload.SlabCount = static_cast<int32_t>( atlas.SlabCount );

        for ( const HeroCloudInstance& hero : m_HeroClouds )
        {
            const auto slab = std::find( bodies.begin(), bodies.end(), hero.Data.Volume );
            if ( slab == bodies.end() )
                continue; // unregistered, or past the atlas — both already said above

            const uint32_t slot = static_cast<uint32_t>( std::distance( bodies.begin(), slab ) );

            const CloudAuthoredPackResult packed = PackCloudAuthoredInstance(
                 hero.WorldTransform, service->GetSizeKm( hero.Data.Volume ), payload.Layer.y, hero.Data,
                 CloudAuthoredAtlasSlabBaseW( slot, atlas.SlabCount ) );

            if ( !packed.Valid )
            {
                LOG_ERROR( "[Clouds] Hero cloud '{}' has a degenerate transform — a scale of zero on some "
                           "axis leaves no way back from the world into the body — so it is not drawn.",
                           hero.Name );
                continue;
            }

            // THE RELATION, STATED RATHER THAN HOPED FOR. The march only samples between the two shells,
            // so a body whose top is above the layer's top is not clipped by anything the artist can see
            // — it is simply never sampled there, and the symptom is a cumulus with its crown sliced flat
            // by an altitude nobody set. Both numbers are in the message, which is the house pattern for
            // two values obliged to agree (desert-engine-verify section 4).
            if ( !CloudAuthoredInstanceFitsLayer( packed.Instance, payload.Layer.z ) && !m_AuthoredFitWarned )
            {
                LOG_WARN( "[Clouds] Hero cloud '{}' spans {:.2f} to {:.2f} km above the layer's base, and the "
                          "layer is {:.2f} km thick starting at {:.2f} km. The part outside the shell is "
                          "never marched, so the body will look cut off there. Move the entity, or give the "
                          "layer a cloud type whose altitudes contain it.",
                          hero.Name, packed.Instance.BoundsMin.y, packed.Instance.BoundsMax.y, payload.Layer.z,
                          payload.Layer.y );
                m_AuthoredFitWarned = true;
            }

            m_AuthoredPayload.Instances[m_AuthoredPayload.Count] = packed.Instance;
            ++m_AuthoredPayload.Count;

            if ( static_cast<uint32_t>( m_AuthoredPayload.Count ) >= kCloudAuthoredSlots )
            {
                // The OTHER limit, and it is worth telling them apart: this one is the march's, paid at
                // every field sample by every instance, where the atlas's is memory.
                if ( m_HeroClouds.size() > kCloudAuthoredSlots && !m_AuthoredCrowdWarned )
                {
                    LOG_WARN( "[Clouds] {} hero clouds are live and the march carries {}; the rest are not "
                              "drawn. Each instance costs a bounds test at every sample of every view ray "
                              "AND of every shadow ray, which is what the limit is for.",
                              m_HeroClouds.size(), kCloudAuthoredSlots );
                    m_AuthoredCrowdWarned = true;
                }
                break;
            }
        }

        // THE RELATION BETWEEN THE PAYLOAD AND THE IMAGE, asserted where both are in hand. An instance
        // packed for one atlas and dispatched against another reads a body at the wrong depth and draws a
        // cloud nobody sculpted, silently — see Graphic::CloudAuthoredPayloadIsBindable.
        if ( !CloudAuthoredPayloadIsBindable( m_AuthoredPayload, atlas.SlabCount ) )
        {
            LOG_ERROR( "[Clouds] The authored payload ({} instances over {} slabs) does not address the "
                       "atlas that is about to be bound ({} slabs); no hero cloud is drawn this frame.",
                       m_AuthoredPayload.Count, m_AuthoredPayload.SlabCount, atlas.SlabCount );
            m_AuthoredPayload = CloudAuthoredPayload{};
            m_AuthoredAtlas   = nullptr;
        }
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

    bool VolumetricCloudRenderer::EnsureNoiseVolumes( const Assets::AssetHandle ( &handles )[kCloudSpeciesSlots],
                                                      uint32_t speciesCount )
    {
        auto* service = Runtime::ResourceRegistry::GetCloudNoiseService();
        auto* types   = Runtime::ResourceRegistry::GetCloudTypeService();

        // Asked EVERY frame rather than cached behind a "have I got one" flag, because the answer changes
        // for three independent reasons now — the artist picks a different cloud TYPE, the type they
        // already picked is edited to name a different volume, or the volume itself is re-baked and
        // hot-reloaded. A flag would answer none of them. Every lookup is a hash-map probe against a
        // handle; they are not worth a cache that can be wrong.
        //
        // THE VOLUME COMES THROUGH THE TYPE, which is the one structural change T1 made to this path: the
        // character of a cloud's edge is a property of what kind of cloud it is, so the type names it and
        // the layer does not. An empty slot on either side lands on the built-in default volume.
        //
        // ONE VOLUME PER SPECIES, AND THIS FUNCTION USED TO TAKE THE FIRST NON-EMPTY SLOT'S AND GIVE IT TO
        // THE WHOLE LAYER. That was the programme's last recorded debt, and it was a dead setting: three
        // of a layer's four slots could name a volume the frame never read, silently, while the Cloud Type
        // panel's own tooltip promised the opposite. What paid for it is four descriptors instead of one —
        // and they cost nothing in memory, because Assets::AssetPreloader uploads every `.dcnv` in the
        // project at startup whatever any scene names.
        //
        // THE HANDLES ARE THE SPECIES' AND NOT THE SLOTS', which matters when a layer's filled slots have
        // holes in them: ResolveSpecies has already compacted slot 3 down to species 1, and the march
        // indexes SpeciesNoise by the same compacted number. Two statements of that compaction is exactly
        // the defect this task found in the code it replaced.
        Assets::AssetHandle perSpecies[kCloudSpeciesSlots] = {};

        const uint32_t species = std::min( speciesCount, kCloudSpeciesSlots );
        for ( uint32_t k = 0; k < species; ++k )
            perSpecies[k] = types->GetNoiseVolume( handles[k] );

        m_NoiseSlots  = ResolveCloudNoiseVolumes( perSpecies, species );
        m_NoiseNeeded = m_NoiseSlots.DistinctCount;

        for ( uint32_t slot = 0; slot < kCloudSpeciesSlots; ++slot )
        {
            Image3D* volume = service->Get( m_NoiseSlots.Volume[slot] );
            if ( !volume )
            {
                // The service has already logged which volume is missing and why. Latched so a scene with a
                // broken reference does not print once per frame forever.
                if ( !m_NoiseFailed )
                {
                    m_NoiseFailed = true;
                    LOG_ERROR( "[Clouds] No noise volume could be resolved for slot {} of this layer (of {} "
                               "distinct volumes over {} species); the clouds will not render for this view "
                               "until one is registered.",
                               slot, m_NoiseNeeded, species );
                }
                return false;
            }

            m_NoiseVolume[slot] = volume;
        }

        // Cleared as soon as the volumes do resolve: the failure above is a state of the SCENE, not of this
        // renderer, and dropping a project's clouds for the rest of the session because one scene was
        // opened with a stale reference is the kind of latch that reads as a broken build.
        m_NoiseFailed = false;

        return true;
    }

    void VolumetricCloudRenderer::ExecuteInFrame()
    {
        DESERT_PROFILE_PASS( "Clouds: ExecuteInFrame" );

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

        // RESOLVED AGAIN RATHER THAN CACHED FROM EnsureModellingVolume. Two hash-map probes per slot
        // against handles that have not moved is not worth a member that can disagree with the volume it
        // was built beside — and the one thing that must not diverge is exactly the set whose channels the
        // volume carries.
        //
        // AND IT IS RESOLVED BEFORE THE NOISE, because the volumes a layer binds are named by its TYPES.
        CloudTypeShape      shapes[kCloudSpeciesSlots]{};
        Assets::AssetHandle handles[kCloudSpeciesSlots]{};
        const uint32_t      speciesCount = ResolveSpecies( shapes, handles );

        if ( !EnsureNoiseVolumes( handles, speciesCount ) )
            return;

        if ( !EnsureModellingVolume() )
            return;

        // THE SAME CEILING THE SHADOW MAP'S BLOCK GOT, from the same one call, because the two passes march
        // the same field and a shadow ray of two different lengths in one frame is the class of
        // disagreement §2.3.1 of the contract is about.
        const CloudQualityScale quality = CloudQualityFor( m_Quality );
        const CloudGpuPayload   payload =
             PackCloudParams( m_Data, shapes, speciesCount, atmosphere, m_WindOffset,
                              CloudRegionBinding{ m_ModellingOriginKm, m_ModellingParams.RegionSizeKm },
                              quality.LightMarchSampleCeiling, quality.StopTransmittanceFloor, m_NoiseSlots );
        m_ParamsBuffer->SetData( &payload, static_cast<uint32_t>( sizeof( payload ) ) );

        // Slot A. Rebuilt here rather than reused from the shadow map's call: the two dispatches sit on
        // opposite sides of the render graph and the shadow map may not have run at all this frame (no
        // casting, no strength, a failed allocation), so a payload built there is a payload that might
        // not exist. It is a handful of matrix inversions for at most four entities.
        BuildAuthoredPayload( payload );
        m_AuthoredBuffer->SetData( &m_AuthoredPayload, static_cast<uint32_t>( sizeof( m_AuthoredPayload ) ) );

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
        // Only the DISTINCT volumes, for the reason the shadow pass gives at its own copy of this loop.
        for ( uint32_t slot = 0; slot < m_NoiseNeeded; ++slot )
            renderer.ComputeImageBeginRead( m_NoiseVolume[slot] );
        renderer.ComputeImageBeginRead( m_ModellingVolume.get() );
        if ( m_AuthoredAtlas )
            renderer.ComputeImageBeginRead( m_AuthoredAtlas );
        renderer.ComputeImageBeginWrite( m_TraceImage.get() );
        renderer.ComputeImageBeginWrite( m_TraceGuideImage.get() );

        m_MarchPipeline->SetOutput( kCloudOutputBinding, m_TraceImage.get(), 0 );
        m_MarchPipeline->SetOutput( kCloudGuideOutputBinding, m_TraceGuideImage.get(), 0 );
        m_MarchPipeline->SetStorageBuffer( kCloudParamsBinding, m_ParamsBuffer.get() );
        m_MarchPipeline->SetInput( kCloudSceneDepthBinding, depthImage );
        // ALL FOUR, always, whatever the layer needs — an unbound sampler is an invalid descriptor set and
        // this backend answers one by skipping the dispatch. Slots past the distinct count repeat slot 0's
        // image, which is what ResolveCloudNoiseVolumes filled them with.
        for ( uint32_t slot = 0; slot < kCloudSpeciesSlots; ++slot )
            m_MarchPipeline->SetInput( kCloudNoiseBindings[slot], m_NoiseVolume[slot] );
        m_MarchPipeline->SetInput( kCloudModellingBinding, m_ModellingVolume.get() );

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

        // Slot A's instance list and the atlas of sculpted bodies it addresses. The image is ALWAYS bound, even
        // when the count is zero and nothing will read it, on exactly the terms the two samplers above
        // are bound on: a declared sampler with no image is an invalid descriptor set, and this backend
        // answers an invalid set by skipping the dispatch — every cloud in the frame would disappear with
        // nothing in the log, which is a rake this subsystem has already stood on.
        m_MarchPipeline->SetStorageBuffer( kCloudAuthoredBinding, m_AuthoredBuffer.get() );
        m_MarchPipeline->SetInput(
             kCloudAuthoredAtlasBinding,
             m_AuthoredAtlas
                  ? m_AuthoredAtlas
                  : FallbackTextures::Get().GetFallbackTexture3D( Core::Formats::ImageFormat::RGBA8F ).get() );

        m_MarchPipeline->SetPushConstants( &push, static_cast<uint32_t>( sizeof( push ) ) );

        const uint32_t traceWidth  = HalfExtent( m_HalfWidth );
        const uint32_t traceHeight = HalfExtent( m_HalfHeight );

        {
            // The march and the temporal resolve shared ONE scope until GPU timing arrived, which is
            // precisely the split the cost breakdown needed: they are a heavy half-res raymarch and a
            // cheap reprojection, and lumping them hid the ratio.
            DESERT_PROFILE_PASS( "Clouds: March" );
            renderer.DispatchComputeInFrame( m_MarchPipeline.get(), GroupCount( traceWidth, kMarchWorkGroupSize ),
                                             GroupCount( traceHeight, kMarchWorkGroupSize ), 1 );
        }

        renderer.ComputeImageEndWrite( m_TraceGuideImage.get() );
        renderer.ComputeImageEndWrite( m_TraceImage.get() );
        if ( m_AuthoredAtlas )
            renderer.ComputeImageEndRead( m_AuthoredAtlas );
        renderer.ComputeImageEndRead( m_ModellingVolume.get() );
        for ( uint32_t slot = m_NoiseNeeded; slot-- > 0; )
            renderer.ComputeImageEndRead( m_NoiseVolume[slot] );
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

        {
            DESERT_PROFILE_PASS( "Clouds: TemporalResolve" );
            renderer.DispatchComputeInFrame( m_ResolvePipeline.get(),
                                             GroupCount( m_HalfWidth, kMarchWorkGroupSize ),
                                             GroupCount( m_HalfHeight, kMarchWorkGroupSize ), 1 );
        }

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
