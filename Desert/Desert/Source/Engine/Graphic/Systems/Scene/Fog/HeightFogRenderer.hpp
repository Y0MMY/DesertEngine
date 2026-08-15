#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>

#include <Engine/ECS/ExponentialHeightFogComponent.hpp>
#include <Engine/Graphic/Fog/FogPayload.hpp>
#include <Engine/Graphic/Materials/Fog/MaterialHeightFog.hpp>
#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/ShaderResources/StorageBuffer.hpp>

#include <cstdint>
#include <memory>

namespace Desert::Graphic::System
{
    /**
     * @brief UE's Exponential Height Fog: one closed-form evaluation per pixel, then an over-composite.
     *
     * Two stages, both the volumetric-cloud subsystem's idioms one size smaller:
     *
     *   S1  EVALUATE  compute, RGBA16F at the target's own size. Reconstructs each pixel's world
     *                 position from the scene depth (presented by ComputeImageBeginRead — the ONE path
     *                 that works in Forward and Deferred, teamlead decision Q5) and evaluates the
     *                 closed-form fog integral of Common/HeightFog.glslh. Premultiplied inscattering in
     *                 .rgb, transmittance in .a.
     *   S2  APPLY     a fullscreen quad registered in RenderPhase::Transparency at
     *                 RenderPassOrder::AtmosphericFog — BEFORE the cloud composite's FarField, so the
     *                 clouds and every particle land OVER the fogged scene rather than under it.
     *
     * WHERE IT RUNS. S1 is an in-frame compute dispatch and must be issued OUTSIDE an open render pass,
     * after the scene depth is finished. SceneRenderer::ExecuteAtmosphericFog() calls ExecuteInFrame()
     * between the deferred block and ExecuteTransparency() — the same point the clouds hold.
     *
     * WHEN SKY PHASE 3 LANDS (the camera aerial-perspective volume), this pass gains the AP sample and
     * composes fog OVER it — UE's exact composition order; the slot was chosen so that change is an
     * edit to this pass, not a rearrangement of the frame.
     *
     * ZERO COST WHEN ABSENT. A scene without the component (or with the fog disabled) dispatches
     * nothing, allocates no fog target, and the apply pass draws nothing — the frame is what it was
     * before this system existed. What Initialize does build regardless is the two pipelines and the
     * 80-byte parameter buffer, the cloud renderer's arrangement exactly; the per-view RGBA16F target
     * is the only real memory, and it is allocated lazily on the first fogged frame with the failure
     * latched — previews and thumbnails build a SceneRenderer each and never carry fog.
     */
    class HeightFogRenderer final : public RenderSystem
    {
    public:
        using RenderSystem::RenderSystem;
        ~HeightFogRenderer() override;

        Common::BoolResultStr Initialize() override;
        void                  Shutdown() override;
        void                  RegisterPasses( RenderGraphBuilder& builder ) override;

        /**
         * @brief This frame's fog settings, from ECS::HeightFogECSSystem.
         *
         * @param present    false when the scene has no fog component at all. Said explicitly rather
         *                   than by omission — the renderer keeps its state across frames, and a
         *                   component deleted mid-session must take its fog with it.
         * @param fogHeightY the fog entity's TransformComponent Y (world units): the fog floor, owned
         *                   by the transform exactly as UE owns it by the component transform.
         */
        void SetFogSettings( bool present, const ECS::ExponentialHeightFogData& data, float fogHeightY );

        /** @brief Stage S1. Must be called outside any render pass, after the scene depth is final. */
        void ExecuteInFrame();

    private:
        bool CreatePipelines();
        // Allocates (or reallocates) the fog image for @p width x @p height. Returns false having
        // logged the reason and latched the failure.
        bool EnsureResources( uint32_t width, uint32_t height );

        std::shared_ptr<ComputePipeline>  m_FogPipeline;
        std::shared_ptr<GraphicsPipeline> m_ApplyPipeline;

        std::unique_ptr<MaterialHeightFog> m_ApplyMaterial;

        std::shared_ptr<Image2D>                        m_FogImage;
        std::shared_ptr<ShaderResources::StorageBuffer> m_ParamsBuffer;

        ECS::ExponentialHeightFogData m_Data{};
        float                         m_FogHeightY = 0.0f;
        bool                          m_Present    = false;

        uint32_t m_FogWidth  = 0;
        uint32_t m_FogHeight = 0;

        bool m_ResourcesFailed = false;

        // True while the last ExecuteInFrame actually produced a fog image. The apply draws nothing
        // without it, rather than compositing a target from three frames ago.
        bool m_HasFrameResult = false;
    };
} // namespace Desert::Graphic::System
