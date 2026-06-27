#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/ShaderResources/StorageBuffer.hpp>

#include <array>
#include <memory>

namespace Desert::Graphic::System
{
    // Eye-adaptation via a COMPUTE log-luminance histogram (UE/Frostbite-style): each frame clears a
    // 256-bin histogram, builds it from the full HDR scene (atomic adds), then resolves it to an average
    // luminance with percentile clipping (rejects bright/dark outliers) and temporally adapts a 1x1
    // luminance image that tonemap turns into exposure. Replaces the old 8x8-grid fragment pass. Runs in
    // the explicit post-process chain (before tonemap), outside any render pass.
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
        } // histogram + 1x1 buffers are viewport-independent

        void SetParams( float adaptSpeed, float minLuma, float maxLuma )
        {
            m_AdaptSpeed = adaptSpeed;
            m_MinLuma    = minLuma;
            m_MaxLuma    = maxLuma;
        }

        // The 1x1 image holding the latest adapted luminance (sampled by tonemap).
        const std::shared_ptr<Image2D>& GetAdaptedLuminanceImage() const
        {
            return m_LumImage[m_ReadIndex];
        }

    private:
        bool CreateResources();

        std::shared_ptr<ShaderResources::StorageBuffer> m_Histogram;
        std::array<std::shared_ptr<Image2D>, 2>         m_LumImage; // ping-pong 1x1

        std::shared_ptr<ComputePipeline> m_ClearPipeline;
        std::shared_ptr<ComputePipeline> m_HistogramPipeline;
        std::shared_ptr<ComputePipeline> m_AveragePipeline;

        int   m_ReadIndex  = 0; // holds the latest adapted luminance after Execute
        float m_AdaptSpeed = 1.5f;
        float m_MinLuma    = 0.02f;
        float m_MaxLuma    = 8.0f;
    };
} // namespace Desert::Graphic::System
