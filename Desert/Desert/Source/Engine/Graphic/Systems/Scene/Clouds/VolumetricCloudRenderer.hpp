#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>

#include <Engine/ECS/VolumetricCloudComponent.hpp>
#include <Engine/Graphic/Clouds/CloudPayload.hpp>
#include <Engine/Graphic/Materials/Clouds/MaterialCloudComposite.hpp>
#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/ShaderResources/StorageBuffer.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>

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
         */
        void SetCloudSettings( bool present, const ECS::VolumetricCloudData& data, const glm::vec3& windOffset );

        /**
         * @brief Stages S0 and S1. Must be called outside any render pass, after the scene depth is final.
         */
        void ExecuteInFrame();

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
        // Points m_NoiseVolume at the volume this layer's slot resolves to, through
        // Runtime::CloudNoiseService. Returns false having logged the reason when there is not even a
        // default to fall back on.
        bool EnsureNoiseVolume();

        std::shared_ptr<ComputePipeline>  m_MarchPipeline;
        std::shared_ptr<ComputePipeline>  m_ResolvePipeline;
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

        // BORROWED, not owned: Runtime::CloudNoiseService owns every noise volume and shares one upload
        // across all views. A raw pointer says that plainly, where a shared_ptr here would suggest this
        // renderer has a say in the image's lifetime and would keep an unloaded volume alive on the device.
        // Refreshed from the service every frame, so a hot reload swaps the image under it with no state of
        // its own to go stale.
        Image3D* m_NoiseVolume = nullptr;

        ECS::VolumetricCloudData m_Data{};
        glm::vec3                m_WindOffset{ 0.0f };
        bool                     m_Present = false;

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
