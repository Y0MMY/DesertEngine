#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/Pipeline.hpp>

#include <memory>

namespace Desert::Graphic::System
{
    // Bloom via a compute mip-chain (Call of Duty / Jimenez): progressive 13-tap downsample of the HDR
    // scene color (with a Karis average + bright-pass on the first mip), then a tent-filtered additive
    // upsample back to mip 0. The mip-0 result (GetBloomImage()) is added in during tonemapping. Runs as
    // part of the explicit post-process chain, outside any render pass (compute dispatches only).
    class BloomRenderer final : public RenderSystem
    {
    public:
        using RenderSystem::RenderSystem;

        virtual Common::BoolResultStr Initialize() override;
        virtual void                  Shutdown() override;

        void RegisterPasses( RenderGraphBuilder& builder ) override
        {
        }

        void Execute();
        void Resize( uint32_t width, uint32_t height );

        void SetThreshold( float threshold )
        {
            m_Threshold = threshold;
        }

        // The mip-0 bloom result, sampled by the tonemap pass. Recreated on resize.
        const std::shared_ptr<Image2D>& GetBloomImage() const
        {
            return m_BloomImage;
        }

    private:
        bool CreateImage( uint32_t width, uint32_t height );
        bool CreatePipelines();

        // Half-resolution chain; capped so the smallest mip stays a sane size.
        static constexpr uint32_t kMaxBloomMips = 6;
        static constexpr float    kFilterRadius = 1.0f; // tent radius (source texels) for upsampling

        std::shared_ptr<Image2D>          m_BloomImage;
        std::shared_ptr<ComputePipeline>  m_DownsamplePipeline;
        std::shared_ptr<ComputePipeline>  m_UpsamplePipeline;

        uint32_t m_MipLevels = 1;
        float    m_Threshold = 1.0f;
    };
} // namespace Desert::Graphic::System
