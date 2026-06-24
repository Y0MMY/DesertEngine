#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/Materials/PostProcessing/MaterialAutoExposure.hpp>

#include <array>
#include <memory>

namespace Desert::Graphic::System
{
    // Eye-adaptation: each frame measures the average scene luminance and lerps a 1x1 "adapted
    // luminance" buffer toward it (ping-pong so it can read last frame's value). Tonemap turns that
    // adapted luminance into an exposure. Part of the explicit post-process chain (runs before tonemap).
    class AutoExposureRenderer final : public RenderSystem
    {
    public:
        using RenderSystem::RenderSystem;

        virtual Common::BoolResultStr Initialize() override;
        virtual void                  Shutdown() override;

        void RegisterPasses( RenderGraphBuilder& builder ) override
        {
        }

        void Execute();
        void Resize( uint32_t, uint32_t )
        {
        } // 1x1 — independent of viewport size

        void SetParams( float adaptSpeed, float minLuma, float maxLuma )
        {
            m_AdaptSpeed = adaptSpeed;
            m_MinLuma    = minLuma;
            m_MaxLuma    = maxLuma;
        }

        // The 1x1 buffer holding the latest adapted luminance (read by tonemap).
        std::shared_ptr<Framebuffer> GetAdaptedLuminanceFramebuffer() const
        {
            return m_LumFB[m_ReadIndex];
        }

    private:
        std::array<std::shared_ptr<Framebuffer>, 2> m_LumFB;
        std::shared_ptr<GraphicsPipeline>           m_Pipeline;
        std::unique_ptr<MaterialAutoExposure>       m_Material;

        int   m_ReadIndex  = 0; // holds the latest adapted luminance after Execute
        float m_AdaptSpeed = 1.5f;
        float m_MinLuma    = 0.02f;
        float m_MaxLuma    = 8.0f;
    };
} // namespace Desert::Graphic::System
