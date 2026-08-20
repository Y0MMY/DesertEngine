#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>

#include <Engine/ECS/VolumetricCloudComponent.hpp>
#include <Engine/Graphic/Clouds/CloudAuthoredPayload.hpp>
#include <Engine/Graphic/Clouds/CloudPayload.hpp>
#include <Engine/Graphic/Clouds/CloudProfileTable.hpp>
#include <Engine/Graphic/Clouds/CloudQuality.hpp>
#include <Engine/Graphic/Clouds/CloudShadowPayload.hpp>
#include <Engine/Graphic/Materials/Clouds/MaterialCloudComposite.hpp>
#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/ShaderResources/StorageBuffer.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace Desert::Graphic::System
{
    /**
     * @brief The volumetric cloud pass: a per-pixel march through a spherical shell around the planet,
     *        composited over the scene at sky distance.
     *
     * Three stages. There used to be a fourth in front of them — a compute bake that filled the noise
     * volume from a seed — and it is gone: the volume is an ASSET now (Engine/Assets/CloudNoiseVolume.hpp),
     * generated offline and uploaded once by Runtime::CloudNoiseService, because a volume that only ever
     * existed on the GPU could not be saved, shown, or replaced by one the artist made.
     *
     *
     *   S1  MARCH      compute, RGBA16F at a QUARTER of the target's size. Reconstructs each pixel's ray
     *                  from the camera's inverse view-projection — through the HALF-resolution pixel this
     *                  frame's sub-pixel offset owns, which is the projection jitter — intersects the
     *                  shell, cuts the segment at the scene depth (presented by ComputeImageBeginRead —
     *                  the one path that works in both Forward and Deferred), and integrates.
     *                  Premultiplied radiance in .rgb, transmittance in .a.
     *   S2  RESOLVE    compute, RGBA16F at HALF the target's size. Unreal's VolumetricRenderTarget mode 0
     *                  reconstruction: a half-res pixel this frame traced is taken exactly, and one it did
     *                  not is reprojected from the previous frame's reconstruction through the guide's
     *                  cloud front distance, validated, and blended. Ping-ponged between two history
     *                  targets because it reads last frame's result while writing this frame's.
     *   SM  SHADOW MAP compute, RGBA32F, 512x512 at the reference quality tier and scaled with it,
     *                  marched down the SUN's direction rather than the eye's,
     *                  and issued EARLY — before the render graph, because the deferred lighting pass
     *                  reads it. Each texel holds (frontDepthKm, meanExtinctionPerKm, maxOpticalDepth),
     *                  the triple that reconstructs a correct transmittance for a receiver at any depth
     *                  inside or below the layer with one fetch. See Common/CloudShadowMap.glslh for the
     *                  encoding and Engine/Graphic/Clouds/CloudShadowPayload.hpp for the projection.
     *                  Independent of S1 and S2: it needs no scene depth and no view.
     *   S3  COMPOSITE  a fullscreen quad registered in RenderPhase::Transparency at
     *                  RenderPassOrder::FarField — ABOVE the height fog and BELOW everything else the
     *                  phase composites, so particles land over the clouds rather than under them. It
     *                  upsamples the HALF-resolution reconstruction, unchanged by mode 0.
     *
     * WHERE IT RUNS. S0, S1 and S2 are in-frame compute dispatches and must be issued OUTSIDE an open
     * render pass, after the scene depth is finished. SceneRenderer::ExecuteVolumetricClouds() calls
     * ExecuteInFrame() immediately after ExecuteAtmosphericFog(), so the depth a ray is cut at and the
     * atmosphere a cloud is lit by both belong to the camera this frame was drawn with.
     *
     * PER-VIEW STATE, AND WHY IT IS SAFE. The history targets, the frame counter and the previous frame's
     * view-projection are MEMBERS OF THIS OBJECT, and SceneRenderer constructs one VolumetricCloudRenderer
     * per renderer. That is strictly stronger than the per (frame x renderer slot) rule in
     * Docs/RENDERER_FRAME_STATE.md: an asset thumbnail's renderer cannot see the viewport's history
     * because it does not own it. The one piece of state that lives in the shared shader-resource layer is
     * the parameter buffer, which is created NON-PERSISTENT and therefore already carries a copy per
     * (frame x slot).
     *
     * ZERO COST WHEN ABSENT. A scene with no cloud component, or the clouds disabled, or no atmosphere to
     * light them, dispatches nothing, allocates nothing and composites nothing: the frame is bit for bit
     * what it was before this system existed. That is checked before any allocation rather than after,
     * because the editor builds a SceneRenderer for every asset thumbnail and every mesh preview, and
     * none of them has a sky.
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
         * @brief This frame's cloud layer, from ECS::VolumetricCloudECSSystem.
         *
         * @param present    false when the scene has no cloud component at all. Said explicitly rather
         *                   than by omission — the renderer keeps its state across frames, and a component
         *                   deleted mid-session must take its clouds with it.
         * @param windOffset the drift the ECS accumulated, world units. Owned there because that is where
         *                   the timestep is.
         * @param heroClouds this frame's sculpted bodies — slot A of the seam. Empty is the ordinary case
         *                   and means the march's authored loop does not run at all.
         * @param quality    the scene's cloud quality tier, refreshed from Core::SceneSettings each
         *                   BeginScene like every other cost-versus-quality choice. It arrives HERE rather
         *                   than being read from a global because this renderer is one of several live at
         *                   once (Docs/RENDERER_FRAME_STATE.md) and a tier is per-view state like any
         *                   other.
         */
        void SetCloudSettings( bool present, const ECS::VolumetricCloudData& data, const glm::vec3& windOffset,
                               Core::CloudQuality quality, const std::vector<HeroCloudInstance>& heroClouds );

        /**
         * @brief Stages S0 and S1. Must be called outside any render pass, after the scene depth is final.
         */
        void ExecuteInFrame();

        /**
         * @brief Stage SM — the cloud shadow map. Must be called outside any render pass, and EARLY:
         *        before the render graph records and before the deferred lighting pass, both of which
         *        read the result. It depends on nothing that the frame produces — no scene depth, no
         *        atmosphere LUT — only on the field, the sun and the camera position, so nothing forces
         *        it later and the one thing that forces it earlier is its consumer.
         *
         * Costs exactly nothing when the layer is absent, disabled, not casting or at zero strength: the
         * image is not even allocated until all four are true.
         */
        void ExecuteShadowMapInFrame();

        /// True when a shadow map was produced for THIS frame and may be sampled. False makes every
        /// consumer fall back to "no cloud shadow" rather than to a map from a frame the sun has since
        /// left.
        bool HasShadowMap() const
        {
            return m_ShadowMapValid;
        }

        /// The map itself. Null unless HasShadowMap(). Borrowed — this renderer owns it.
        Image2D* GetShadowMapImage() const
        {
            return m_ShadowMapImage.get();
        }

        /// This frame's projection and its far depth, for a consumer that has to transform a world
        /// position into the map. Meaningless unless HasShadowMap().
        const CloudShadowMapView& GetShadowMapView() const
        {
            return m_ShadowMapView;
        }

        /// The artist's shadow strength, applied by the CONSUMER rather than baked into the map — see
        /// Common/CloudShadowMap.glslh. Zero when the layer is not casting at all, so a consumer that
        /// only reads this number still gets the right answer.
        float GetShadowStrength() const;

    private:
        bool CreatePipelines();
        // Allocates (or reallocates) all SIX images the pass owns: the quarter-resolution scatter and
        // guide the march writes, and the two half-resolution scatter/guide pairs the resolve ping-pongs
        // between. One function because they are one set — a size is a property of the view, and a guide
        // that outlived a resized scatter, or a history that outlived a resized trace, would hand the next
        // stage last frame's edges. Reallocating also invalidates the history, because the bytes in a
        // freshly created image mean nothing. Returns false having logged the reason and latched the
        // failure.
        bool EnsureTraceTargets( uint32_t halfWidth, uint32_t halfHeight );
        // Points m_NoiseVolume at the volume this layer's FIRST cloud type names, through
        // Runtime::CloudTypeService and then Runtime::CloudNoiseService. Returns false having logged the
        // reason when there is not even a default to fall back on.
        bool EnsureNoiseVolume();
        // Allocates the shadow map the first frame the layer actually casts, and REALLOCATES it when the
        // quality tier changes its size. Separate from EnsureTraceTargets because its size is not a
        // property of the view: a viewport resize must not throw it away, where every one of the six trace
        // targets IS the view's size and must. Returns false having logged the reason and latched the
        // failure.
        //
        // @param resolution texels per side for THIS tier, from Graphic::CloudShadowResolutionForScale.
        bool EnsureShadowMap( uint32_t resolution );

        /**
         * The types in this layer's four slots, packed down to a prefix, with the empty ones removed and
         * an all-empty layer answered by ONE built-in default.
         *
         * PACKED AND NOT SPARSE, which is the one decision in this function. A layer with slots 1 and 3
         * filled behaves exactly like a layer with slots 1 and 2 filled: the count is a count, the loop in
         * the march has no holes to skip, and a species' identity is its position in the packed set rather
         * than in the panel. The one thing that DOES follow the packed position is the placement field's
         * decorrelation offset — so moving a type from slot 3 to slot 2 moves its clouds, which is the same
         * thing that happens when the artist changes its scale and is visible for the same reason.
         *
         * @param shapes  filled from the front with the resolved shapes.
         * @param handles filled from the front with the handles they came from — the cache key of the
         *                profile table, and the reason this returns both.
         * @return how many of the two arrays were written, always at least 1.
         */
        uint32_t ResolveSpecies( CloudTypeShape ( &shapes )[kCloudSpeciesSlots],
                                 Assets::AssetHandle ( &handles )[kCloudSpeciesSlots] ) const;
        // Builds and uploads the vertical profile table for this layer's cloud type, and only when that
        // type — or the file behind it — has changed. The table is a pure function of the type's twelve
        // numbers (Graphic::CloudBuildProfileTable), so rebuilding it per frame would be 16 384 curve
        // evaluations and a synchronous staging upload for an answer that is identical. Returns false
        // having logged the reason when the image could not be created.
        bool EnsureProfileTable();

        /**
         * Turns this frame's hero clouds into the instance buffer the two cloud passes read, and points
         * m_AuthoredVolume at the body they share.
         *
         * ONE VOLUME PER FRAME, and the limit is the shader's single `sampler3D` rather than a decision
         * taken here: several instances of the SAME body are free and are the ordinary case, several
         * DIFFERENT bodies need the atlas that phase A2 builds. An entity naming a second body is
         * DROPPED AND NAMED in the log rather than drawn with somebody else's shape, which would be a
         * cloud the artist cannot account for.
         *
         * @param payload the layer, already packed. Read for two numbers and neither is recomputed here:
         *                Layer.y, the shell's base altitude, which is what puts an instance into the
         *                field's own frame, and Layer.z, its thickness, which the fit warning is stated
         *                against.
         */
        void BuildAuthoredPayload( const CloudGpuPayload& payload );

        std::shared_ptr<ComputePipeline>  m_MarchPipeline;
        std::shared_ptr<ComputePipeline>  m_ResolvePipeline;
        std::shared_ptr<ComputePipeline>  m_ShadowMapPipeline;
        std::shared_ptr<GraphicsPipeline> m_CompositePipeline;

        std::unique_ptr<MaterialCloudComposite> m_CompositeMaterial;

        // The march's two QUARTER-resolution outputs, allocated and released together because neither is
        // usable alone: the scatter image (premultiplied radiance in .rgb, transmittance in .a) and the
        // depth guide beside it (.x cloud front distance, .y scene distance, both kilometres). Declared
        // adjacently and with no comment between them so the alignment of this block stays one group — a
        // comment inserted mid-group is what clang-format and this repository's style disagree about.
        std::shared_ptr<Image2D>                        m_TraceImage;
        std::shared_ptr<Image2D>                        m_TraceGuideImage;
        std::shared_ptr<ShaderResources::StorageBuffer> m_ParamsBuffer;
        std::shared_ptr<ShaderResources::StorageBuffer> m_ResolveParamsBuffer;

        // The half-resolution reconstruction, ping-ponged. The resolve reads index 1 - write and writes
        // index write, so one frame's result is the next frame's history; the composite always samples the
        // one just written. Two images rather than one because a single target would be read and written
        // by the same dispatch at different coordinates, which is a race with no defined answer.
        std::shared_ptr<Image2D> m_HistoryImage[2];
        std::shared_ptr<Image2D> m_HistoryGuideImage[2];

        // THE CLOUD SHADOW MAP and the parameter copy its dispatch reads.
        //
        // A SECOND PARAMETER BUFFER FOR THE SAME BYTES, and it is not duplicated state: both buffers are
        // filled from one call of the pure Graphic::PackCloudParams, on the same m_Data, in the same
        // frame. What forces two of them is the frame, not the data — this pass is issued before the
        // render graph and the march after it, and writing one non-persistent buffer twice between two
        // dispatches is a hazard whose only defence today is that the bytes happen to be equal.
        std::shared_ptr<Image2D>                        m_ShadowMapImage;
        std::shared_ptr<ShaderResources::StorageBuffer> m_ShadowParamsBuffer;

        // This frame's projection, and whether it describes anything. Rebuilt every frame rather than
        // cached: it is a pure function of the camera, the sun and the snap, and a cached matrix is the
        // shape of state that survives a scene reload it should not have.
        CloudShadowMapView m_ShadowMapView{};
        bool               m_ShadowMapValid  = false;
        bool               m_ShadowMapFailed = false;

        // BORROWED, not owned: Runtime::CloudNoiseService owns every noise volume and shares one upload
        // across all views. A raw pointer says that plainly, where a shared_ptr here would suggest this
        // renderer has a say in the image's lifetime and would keep an unloaded volume alive on the device.
        // Refreshed from the service every frame, so a hot reload swaps the image under it with no state of
        // its own to go stale.
        Image3D* m_NoiseVolume = nullptr;

        // SLOT A. The instance buffer is doubled for the same reason the parameter buffer is — the shadow
        // map dispatches before the render graph and the march after it, and one non-persistent buffer
        // written twice between two dispatches is a hazard whose only defence would be that the bytes
        // happen to be equal. The volume itself is BORROWED from Runtime::CloudModellingService, like the
        // noise volume beside it, and is null in every scene that has no hero cloud in it — in which case
        // the fallback volume is bound instead and the count is zero.
        std::shared_ptr<ShaderResources::StorageBuffer> m_AuthoredBuffer;
        std::shared_ptr<ShaderResources::StorageBuffer> m_ShadowAuthoredBuffer;

        std::vector<HeroCloudInstance> m_HeroClouds;
        CloudAuthoredPayload           m_AuthoredPayload{};
        Image3D*                       m_AuthoredVolume = nullptr;

        // Latched so that a body standing outside its layer is said ONCE per scene rather than sixty
        // times a second, and re-armed the moment the arrangement changes so that fixing it and breaking
        // it again both speak.
        bool m_AuthoredFitWarned = false;
        bool m_AuthoredMixWarned = false;

        // The vertical profile table this view marches against — OWNED, unlike the noise volume, because
        // it is GENERATED here rather than resolved from an asset: the type ships twelve numbers, not
        // sixteen thousand texels (decision D-13). 64 KiB per view, which is two thousandths of the
        // subsystem's budget.
        std::shared_ptr<Image2D> m_ProfileTable;
        // WHAT THE TABLE WAS BUILT FROM, and it takes two values rather than one. The handle answers "is
        // this still the type the artist chose"; the generation answers "is the FILE behind that type still
        // the one I read", which is what makes an edit in the Cloud Type panel show up in the viewport
        // without the handle changing at all. A null handle with generation 0 is the "no table yet" state,
        // and it is unreachable as a real answer because a registered type always bumps the generation
        // past zero.
        // FOUR HANDLES NOW, in the packed order ResolveSpecies produced them, plus how many of them were
        // real. Comparing the whole set rather than one handle is what makes dropping a second type into
        // the layer rebuild the table: the first slot has not changed, and a cache keyed on it alone would
        // show a one-species sky until something else happened to invalidate it.
        Assets::AssetHandle m_ProfileTypes[kCloudSpeciesSlots]{};
        uint32_t            m_ProfileSpeciesCount = 0;
        uint32_t            m_ProfileGeneration   = 0;

        ECS::VolumetricCloudData m_Data{};
        glm::vec3                m_WindOffset{ 0.0f };
        bool                     m_Present = false;

        // This view's quality tier and the numbers it derives. The scale is held rather than re-derived at
        // every use because EnsureShadowMap has to notice when it CHANGES — the map's size is a property
        // of the tier, so switching tiers mid-session reallocates it, which is the one thing the old
        // "allocated once and never again" comment on that function stopped being true about.
        Core::CloudQuality m_Quality             = Core::CloudQuality::High;
        float              m_ShadowMapScaleInUse = 0.0f;

        // The HALF-resolution grid, which is what the sub-pixel jitter and the reconstruction are both
        // expressed in. The trace's own extents are this rounded up again and are derived where needed
        // rather than stored, so the two cannot drift apart on an odd viewport.
        uint32_t m_HalfWidth  = 0;
        uint32_t m_HalfHeight = 0;

        // Latched by EnsureTraceTargets and covering ALL SIX images: any one missing means the pass cannot
        // run, and retrying an allocation that already failed once per frame only fills the log.
        bool m_TargetsFailed = false;
        bool m_NoiseFailed   = false;

        // Advances once per executed frame. It decides both the march's dither pattern and which of the
        // four sub-pixels this frame traces, and it selects the history target written. Wrapping is
        // harmless: only its low bits reach the hash, the sub-pixel walk and the ping-pong, and all three
        // have a period that divides 2^32.
        uint32_t m_FrameIndex = 0;

        // The view-projection of the frame that WROTE the history — the previous EXECUTED frame, not the
        // previous frame of the application. The two differ whenever the pass is skipped (no component, no
        // atmosphere, an allocation failure), and reprojecting through a matrix from a frame that did not
        // write the history is how a history buffer starts smearing after a scene is reloaded.
        glm::mat4 m_PrevViewProjection{ 1.0f };

        // False until a reconstruction has been written into the targets currently allocated. Reading the
        // history before that is reading uninitialised device memory, so the resolve is handed the
        // engine's fallback texture instead and told to ignore it: an unbound sampler would be an INVALID
        // descriptor set, which this backend answers by skipping the whole dispatch.
        bool m_HistoryValid = false;

        // True while the last ExecuteInFrame actually produced a reconstruction. The composite draws
        // nothing without it, rather than compositing a target from three frames ago over a scene that
        // has since moved.
        bool m_HasFrameResult = false;

        // Which of the two history slots the last executed resolve wrote, and therefore the one the
        // composite must sample. An index rather than a second shared_ptr, so each image has exactly one
        // owner and a resize cannot leave the composite holding a released target.
        uint32_t m_ResolvedIndex = 0;
    };
} // namespace Desert::Graphic::System
