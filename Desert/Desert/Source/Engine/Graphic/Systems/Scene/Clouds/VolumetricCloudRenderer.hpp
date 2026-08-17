#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>

#include <Engine/Graphic/Clouds/CloudLayerSet.hpp>
#include <Engine/Graphic/Clouds/CloudNoiseVolumes.hpp>
#include <Engine/Graphic/Clouds/CloudPayload.hpp>
#include <Engine/Graphic/Clouds/CloudProfileCurves.hpp>
#include <Engine/Graphic/Clouds/CloudVolumeAtlas.hpp>
#include <Engine/Graphic/Clouds/CloudVolumeInstance.hpp>
#include <Engine/Graphic/Clouds/CloudVolumePlacement.hpp>
#include <Engine/Graphic/Materials/Clouds/MaterialVolumetricClouds.hpp>
#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/ECS/VolumetricCloudsComponent.hpp>
#include <Engine/ShaderResources/StorageBuffer.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Desert::Graphic::System
{
    /**
     * @brief The volumetric cloud layer: weather map, raymarch, temporal resolve, composite.
     *
     * All four in-frame stages of the subsystem live here:
     *
     *   S1  WEATHER MAP  compute, 512x512xN RGBA8 — ONE SLICE PER CLOUD LAYER, filled by one dispatch.
     *                    Regenerated only when a field of some layer's Weather group changes — nothing in
     *                    it depends on the camera or on the clock, so a per-frame dispatch would be a
     *                    per-frame dispatch producing identical bytes.
     *   S2  RAYMARCH     compute, RGBA16F at ResolutionScale. One ray per pixel through the scene's
     *                    cloud shells, walked in the order the ray meets them (CloudPlanTwoShells) and
     *                    clamped to the scene depth. Premultiplied radiance + transmittance, plus
     *                    an RGBA8 guide recording how far each ray was allowed to run. At Full resolution
     *                    with the temporal stage running, the march is CHECKERBOARDED: half the pixels
     *                    each frame, the other half reconstructed by S3 (CloudCheckerboardActive) — a
     *                    documented property of the tier, not a quality knob.
     *   S3  TEMPORAL     compute, RGBA16F at ResolutionScale, ONLY when TemporalMode is Reprojection.
     *                    Reprojects a two-image history by the camera's motion, clamps it to the current
     *                    3x3 neighbourhood and blends; checkerboard-stale pixels take the clamped history
     *                    outright. With TemporalMode = Off this stage does not run, holds no memory, and
     *                    the composite reads S2's output directly — which is what makes "Off is the
     *                    marched image, bit for bit" true by construction.
     *   S4  COMPOSITE    a fullscreen quad registered in RenderPhase::Transparency at
     *                    RenderPassOrder::FarField, so it lands under the particle billboards. It
     *                    magnifies whichever image the temporal mode selected, weighting its taps by the
     *                    guide so the magnification does not smear across a geometry silhouette.
     *
     * WHERE IT RUNS. S1..S3 are in-frame compute dispatches and must be issued OUTSIDE an open
     * render pass, after the scene depth is finished. SceneRenderer::ExecuteVolumetricClouds() calls
     * ExecuteInFrame() between the deferred block and ExecuteTransparency(), which is the one point in
     * the frame where both hold in Forward and in Deferred.
     *
     * TWO LAYERS, ONE MARCH. A scene gets a second cloud layer by putting a second Volumetric Clouds
     * component on a second entity; the collector hands them over in altitude order and this renderer
     * marches both in ONE dispatch. Not two: two dispatches would need two scatter targets, two history
     * pairs and two composites, and no amount of compositing afterwards can resolve a cirrus streak that
     * crosses in front of one cumulus tower and behind another. The four per-layer tables — weather map,
     * profile map, profile table, shadow map — are volumes with one slice each.
     *
     * WHAT IT OWNS AND WHAT IT BORROWS. It owns the weather map, the scatter target, the depth guide, the
     * two history images, the parameter buffer and its pipelines. It BORROWS the three noise volumes
     * (Graphic::CloudNoiseVolumes, shared process-wide and leased by ECS::CloudNoiseECSSystem), the sky
     * parameter buffer (an opaque handle on AtmosphereEnv) and the scene depth attachment.
     *
     * ALLOCATION IS LAZY, and the failure is LATCHED. The editor builds a SceneRenderer per asset
     * thumbnail and one for the Details mesh preview; anything allocated in Initialize is paid for once
     * per preview, and a preview never has a cloud component. The images appear on the first frame that
     * actually marches — the shape EnsureSSRResources established — and a failure is logged once, with
     * its numbers, and never retried per frame. The history pair is allocated later still, on the first
     * frame that actually resolves, and is released the moment the mode leaves Reprojection: "Off costs
     * nothing" has to stay true after the artist has switched it off, not only before they switched it on.
     */
    class VolumetricCloudRenderer final : public RenderSystem
    {
    public:
        using RenderSystem::RenderSystem;
        ~VolumetricCloudRenderer() override;

        Common::BoolResultStr Initialize() override;
        void                  Shutdown() override;
        void                  RegisterPasses( RenderGraphBuilder& builder ) override;

        /**
         * @brief This frame's cloud layers, from ECS::VolumetricCloudsECSSystem.
         *
         * @param layers  the scene's enabled cloud layers, in ALTITUDE ORDER. Count 0 means the scene has
         *                no volumetric-clouds component at all, or every one of them is switched off —
         *                said explicitly rather than by omission, because the renderer keeps its state
         *                across frames and a component deleted mid-session would otherwise leave the last
         *                cloudscape it saw hanging in the sky.
         * @param volumes this frame's placed hero clouds, already sorted shadow-casters-first by
         *                ECS::VolumetricCloudsECSSystem — the shadow pass marches a prefix of the buffer
         *                and the ordering is what makes that prefix mean "casts a cloud shadow".
         *                Passed even when empty: an empty list releases every atlas tile.
         */
        void SetCloudSettings( const CloudLayerSet& layers, const CloudVolumePlacements& volumes );

        /** @brief Stages S1 and S2. Must be called outside any render pass. */
        void ExecuteInFrame();

    private:
        // The Weather-group fields the map is baked from. Compared field by field to decide whether S1
        // has to run again; a hash would be shorter and would tell us nothing when it collided.
        struct WeatherFingerprint
        {
            float   Coverage          = -1.0f;
            float   CoverageContrast  = -1.0f;
            float   WarpStrength      = -1.0f;
            float   CloudType         = -1.0f;
            float   CloudTypeVariance = -1.0f;
            float   Wetness           = -1.0f;
            int32_t Seed              = -1;
            int32_t Octaves           = -1;

            bool operator==( const WeatherFingerprint& ) const = default;
        };

        static WeatherFingerprint FingerprintOf( const ECS::VolumetricCloudData& data );

        // The authored curves the Cloud Type table is baked from. Compared field by field for the same
        // reason the weather fingerprint is: nine vectors are cheap to compare and a hash that collided
        // would leave the sky showing a form nobody authored, with nothing to look at.
        struct ProfileFingerprint
        {
            glm::vec4 Stratus{ -1.0f };
            glm::vec4 Shelf{ -1.0f };
            glm::vec4 Stratocumulus{ -1.0f };
            glm::vec4 Cumulus{ -1.0f };
            glm::vec4 Congestus{ -1.0f };
            glm::vec4 Anvil{ -1.0f };
            glm::vec3 ShelfForm{ -1.0f };
            glm::vec3 CongestusForm{ -1.0f };
            glm::vec3 AnvilForm{ -1.0f };

            bool operator==( const ProfileFingerprint& ) const = default;
        };

        static ProfileFingerprint ProfileFingerprintOf( const ECS::VolumetricCloudData& data );

        // Bakes the Cloud Type table and uploads it, or returns false having logged and latched. Called
        // from EnsureResources, so the cost lands on the first marched frame and on a curve edit, never
        // per frame — the curves change when an artist drags one, and that is all.
        bool EnsureProfileLut();

        bool CreatePipelines();
        // Allocates (or reallocates) the weather map, the scatter target and the depth guide for
        // @p width x @p height at the current resolution tier. Returns false having logged the reason and
        // latched the failure.
        bool EnsureResources( uint32_t width, uint32_t height );
        // Allocates the two history images at the scatter target's size, or releases them when the mode
        // no longer uses them. Returns true when the pair is present and usable this frame.
        bool EnsureHistory();
        void ReleaseHistory();

        void DispatchWeather();
        // Fills the sun-space shadow map from the weather map and the shape noise. Runs before the
        // raymarch, which reads it in one fetch where it used to march a cone.
        void DispatchShadowMap( const CloudNoiseSet& noise );
        // @p checkerboard: both stages are handed the SAME answer from CloudCheckerboardActive — the
        // march skips the stale half only when the resolve that reconstructs it is going to run.
        void DispatchRaymarch( const CloudNoiseSet& noise, Image2D* depthImage, bool checkerboard );
        void DispatchTemporalResolve( bool checkerboard );

        std::shared_ptr<ComputePipeline> m_WeatherPipeline;
        std::shared_ptr<ComputePipeline> m_ShadowPipeline;

        // ONE RAYMARCH PIPELINE PER LIVE LAYER COUNT, indexed by count - 1. Same shader, same SPIR-V, same
        // cache entry: what differs is the specialization constant the driver folds the layer machinery
        // against (Graphic::kCloudLayerCountConstantId). A one-layer sky then runs a module that does not
        // contain the two-layer loop at all, which is the only arrangement that gets its frame time back —
        // see the note on the two loops in Programs/Clouds/CloudRaymarch.shader.
        //
        // Built up front, both of them, rather than on demand: creating a pipeline mid-frame stalls on
        // vkCreateComputePipelines, and there are exactly kCloudMaxLayers of them.
        std::array<std::shared_ptr<ComputePipeline>, kCloudMaxLayers> m_RaymarchPipelines;

        std::shared_ptr<ComputePipeline>  m_TemporalPipeline;
        std::shared_ptr<GraphicsPipeline> m_CompositePipeline;

        // The pipeline that marches @p liveLayers layers, or nullptr if it could not be built. The clamp
        // is the SAME one CloudPackPayload applies to LayerCount, so the pipeline and the buffer cannot
        // describe different layer counts.
        [[nodiscard]] ComputePipeline* RaymarchPipelineFor( uint32_t liveLayers ) const;

        std::unique_ptr<MaterialVolumetricClouds> m_CompositeMaterial;

        // THE FOUR PER-LAYER TABLES ARE VOLUMES, kCloudMaxLayers slices deep: the weather map, the
        // second weather image (per-cell Min/Max Height), the authored Cloud Type curve table and the
        // sun-space shadow map. Each is a function of ONE layer's settings, so a deck and a high sheet
        // cannot share one; a volume rather than N images because the engine refuses arrayed samplers
        // and one sampler means one fetch instead of a per-pixel branch (CloudLayerSliceW).
        std::shared_ptr<Image3D> m_WeatherMap;
        std::shared_ptr<Image3D> m_ProfileMap;
        std::shared_ptr<Image3D> m_ProfileLut;
        std::shared_ptr<Image3D> m_CloudShadowMap;
        std::shared_ptr<Image2D> m_ScatterImage;
        std::shared_ptr<Image2D> m_DepthGuideImage;

        // The temporal ping-pong. m_HistoryWrite indexes the one this frame RESOLVES INTO, which is also
        // the one the composite reads; the other is last frame's answer. Two images and not one: the
        // reprojected read lands wherever the camera moved it, so a pixel being written is a pixel some
        // other invocation may still be sampling.
        std::shared_ptr<Image2D> m_HistoryImages[2];
        uint32_t                 m_HistoryWrite = 0;
        // False until a resolve has actually filled the image the NEXT frame will read. Without it the
        // first blended frame would mix in whatever the allocator left in that memory.
        bool m_HistoryFilled = false;
        bool m_HistoryFailed = false;

        /**
         * Turn this frame's placements into atlas leases and instance records.
         *
         * Runs on the render thread, from ExecuteInFrame, because it resolves asset handles through the
         * CloudVolumeService and can create a 32 MiB GPU image. Diffs against the leases held last frame:
         * a placement whose `.dvol` is already resident costs nothing, a new one takes a lease, and a
         * handle nobody references any more gives its tile back. The atlas rebuilds its image only when
         * the resident SET changes, so moving a hero cloud around is free.
         */
        void UpdateVolumeInstances();

        // Binds the hero-cloud atlas and instance buffer on @p pipeline. Called by BOTH the march and the
        // shadow pass, from one place, because the two declare the same two bindings through the same
        // density header and a binding number written twice is a binding number free to disagree.
        void BindHeroVolumes( ComputePipeline* pipeline );

        std::shared_ptr<ShaderResources::StorageBuffer> m_ParamsBuffer;
        // Always allocated at kMaxCloudVolumeInstances records and always bound, whatever the scene
        // holds: a declared descriptor with nothing bound is an invalid set, and 640 bytes is not a
        // number worth making conditional.
        std::shared_ptr<ShaderResources::StorageBuffer> m_VolumeInstanceBuffer;

        // The GPU home of the hero clouds. A plain member and NOT a singleton, unlike CloudNoiseVolumes:
        // the noise set is genuinely process-wide (several scenes share one, keyed by seed), while an
        // atlas belongs to the renderer that marches it.
        CloudVolumeAtlas m_VolumeAtlas;

        // The `.dvol` handles this renderer currently holds a lease on, in instance order. The diff
        // against next frame's placements is what decides which tiles are acquired and released.
        std::vector<uint64_t> m_VolumeLeases;

        // THIS FRAME'S LAYERS, in altitude order and already filtered to the enabled ones. Count == 0 is
        // "no cloud layer in this scene", which is why there is no separate `present` flag any more: one
        // number answers both questions and two could disagree.
        CloudLayerSet         m_Layers{};
        CloudVolumePlacements m_VolumePlacements;
        CloudVoxelCounts      m_VolumeCounts{};

        // A handle whose asset could not be resolved or leased is reported ONCE per handle. Without this
        // a missing `.dvol` is a log line at 60 Hz, which is the same as no log line at all.
        std::vector<uint64_t> m_VolumeFailures;

        // The size and tier m_ScatterImage was built for. A change in either rebuilds it.
        uint32_t                  m_ScatterWidth  = 0;
        uint32_t                  m_ScatterHeight = 0;
        ECS::CloudResolutionScale m_ScatterScale  = ECS::CloudResolutionScale::Half;

        // ONE FINGERPRINT PER LAYER, plus the count they were baked at. The bake fills every slice in one
        // dispatch, so any layer's weather changing rebakes them all — the alternative is a per-slice
        // dispatch for a pass that runs when an artist drags a slider, which is not a rate worth
        // optimising for.
        std::array<WeatherFingerprint, kCloudMaxLayers> m_WeatherBaked{};
        uint32_t                                        m_WeatherBakedCount = 0;
        bool                                            m_WeatherValid      = false;

        std::array<ProfileFingerprint, kCloudMaxLayers> m_ProfileLutBaked{};
        uint32_t                                        m_ProfileLutBakedCount = 0;

        // How many slices of the shadow map this frame's dispatch fills: one past the highest layer
        // whose own Cloud Shadow Map is on. Decided in ExecuteInFrame and consumed by DispatchShadowMap,
        // which is the pair the two would otherwise have to agree about through the component twice.
        uint32_t m_ShadowSlices = 0;

        bool m_ResourcesFailed = false;

        // True while the last ExecuteInFrame actually produced a cloud image. The composite draws
        // nothing without it, rather than sampling a target from three frames ago.
        bool m_HasFrameResult = false;

        // Which image the composite magnifies this frame. Decided once, by CloudSelectCompositeSource,
        // from the mode and from whether the history is actually there.
        CloudCompositeSource m_CompositeSource = CloudCompositeSource::Raymarch;

        // The camera the last resolved frame was rendered from. Reprojection is the difference between
        // this and the current one, and nothing else — there are no motion vectors (CLD-32a).
        glm::mat4 m_PreviousViewProjection{ 1.0f };

        uint32_t m_FrameIndex = 0;

        // Each of these describes a SCENE, not a frame, so each is said once. A per-frame warning about
        // a missing component is how a real message becomes invisible.
        bool m_AtmosphereWarned = false;
        bool m_NoiseWarned      = false;
    };
} // namespace Desert::Graphic::System
