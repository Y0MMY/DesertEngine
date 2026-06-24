#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>

#include <Engine/Graphic/Renderer.hpp>

#include <Engine/Graphic/Materials/PostProcessing/MaterialBloom.hpp>

#include <memory>
#include <vector>

namespace Desert::Graphic::System
{
    // Bloom: bright-pass on the HDR scene color, then a few separable-Gaussian blur passes. The result
    // (GetSystemFramebuffer()) is added back during tonemapping. Part of the explicit post-process chain.
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

    private:
        bool CreateFramebuffers( uint32_t width, uint32_t height );
        bool CreatePipelines();
        void RunQuad( const std::shared_ptr<Framebuffer>& target, const std::string& debugName,
                      const GraphicsPipeline* pipeline, const MaterialExecutor* executor );

        static constexpr int   kIterations = 2;   // H+V blur iterations
        static constexpr float kSpread     = 2.0f; // texels per tap step

        std::shared_ptr<Framebuffer>      m_BrightFB;
        std::shared_ptr<Framebuffer>      m_BlurTemp; // ping-pong A; m_Framebuffer (base) is B / output
        std::shared_ptr<GraphicsPipeline> m_BrightPipeline;
        std::shared_ptr<GraphicsPipeline> m_BlurPipeline;

        std::unique_ptr<MaterialBloomBright>            m_MaterialBright;
        std::vector<std::unique_ptr<MaterialBloomBlur>> m_BlurMaterials; // 2 * kIterations instances

        float m_Threshold = 1.0f;
    };
} // namespace Desert::Graphic::System
