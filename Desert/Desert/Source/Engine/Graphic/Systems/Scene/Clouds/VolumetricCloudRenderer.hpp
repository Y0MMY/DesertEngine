#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>

#include <Engine/Graphic/Clouds/CloudNoiseVolumes.hpp>
#include <Engine/Graphic/Clouds/CloudPayload.hpp>
#include <Engine/Graphic/Materials/Clouds/MaterialVolumetricClouds.hpp>
#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/ECS/VolumetricCloudsComponent.hpp>
#include <Engine/ShaderResources/StorageBuffer.hpp>

#include <cstdint>
#include <memory>

namespace Desert::Graphic::System
{
    /**
     * @brief The volumetric cloud layer: weather map, raymarch, temporal resolve, composite.
     *
     * All four in-frame stages of the subsystem live here:
     *
     *   S1  WEATHER MAP  compute, 512x512 RGBA8. Regenerated only when a field of the Weather group
     *                    changes — nothing in it depends on the camera or on the clock, so a per-frame
     *                    dispatch would be a per-frame dispatch producing identical bytes.
     *   S2  RAYMARCH     compute, RGBA16F at ResolutionScale. One ray per pixel through a spherical
     *                    shell, clamped to the scene depth. Premultiplied radiance + transmittance, plus
     *                    an RGBA8 guide recording how far each ray was allowed to run.
     *   S3  TEMPORAL     compute, RGBA16F at ResolutionScale, ONLY when TemporalMode is Reprojection.
     *                    Reprojects a two-image history by the camera's motion, clamps it to the current
     *                    3x3 neighbourhood and blends. With TemporalMode = Off this stage does not run,
     *                    holds no memory, and the composite reads S2's output directly — which is what
     *                    makes "Off is the marched image, bit for bit" true by construction.
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
         * @brief This frame's cloud settings, from ECS::VolumetricCloudsECSSystem.
         *
         * @param present false when the scene has no volumetric-clouds component at all. Said
         *                explicitly rather than by omission: the renderer keeps its state across frames,
         *                so a component deleted mid-session would otherwise leave the last cloudscape it
         *                saw hanging in the sky.
         */
        void SetCloudSettings( bool present, const ECS::VolumetricCloudData& data );

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
        void DispatchRaymarch( const CloudNoiseSet& noise, Image2D* depthImage );
        void DispatchTemporalResolve();

        std::shared_ptr<ComputePipeline>  m_WeatherPipeline;
        std::shared_ptr<ComputePipeline>  m_ShadowPipeline;
        std::shared_ptr<ComputePipeline>  m_RaymarchPipeline;
        std::shared_ptr<ComputePipeline>  m_TemporalPipeline;
        std::shared_ptr<GraphicsPipeline> m_CompositePipeline;

        std::unique_ptr<MaterialVolumetricClouds> m_CompositeMaterial;

        std::shared_ptr<Image2D> m_WeatherMap;
        std::shared_ptr<Image2D> m_CloudShadowMap;
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

        std::shared_ptr<ShaderResources::StorageBuffer> m_ParamsBuffer;

        ECS::VolumetricCloudData m_Data{};
        bool                     m_Present = false;

        // The size and tier m_ScatterImage was built for. A change in either rebuilds it.
        uint32_t                  m_ScatterWidth  = 0;
        uint32_t                  m_ScatterHeight = 0;
        ECS::CloudResolutionScale m_ScatterScale  = ECS::CloudResolutionScale::Half;

        WeatherFingerprint m_WeatherBaked{};
        bool               m_WeatherValid = false;

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
