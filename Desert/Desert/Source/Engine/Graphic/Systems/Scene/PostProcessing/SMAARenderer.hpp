#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/Materials/PostProcessing/MaterialSMAA.hpp>

namespace Desert::Graphic::System
{
    // SMAA 1x post-process (3 passes: edge detection -> blend-weight calculation -> neighborhood
    // blending). Reads the tonemapped LDR image (m_TargetFramebuffer) and writes the anti-aliased result
    // into its own framebuffer (GetSystemFramebuffer()). Runs only when SceneSettings.AA == SMAA. Uses the
    // precomputed AreaTex/SearchTex LUTs loaded from Resources/Textures/SMAA.
    class SMAARenderer final : public RenderSystem
    {
    public:
        using RenderSystem::RenderSystem;

        virtual Common::BoolResultStr Initialize() override;
        virtual void                  Shutdown() override {}

        void RegisterPasses( RenderGraphBuilder& builder ) override {}

        void Execute();
        void Resize( uint32_t width, uint32_t height );

    private:
        void LoadLUTs();

        // Intermediate targets (final output is the base m_Framebuffer).
        std::shared_ptr<Framebuffer> m_EdgesFB;
        std::shared_ptr<Framebuffer> m_WeightsFB;

        std::shared_ptr<GraphicsPipeline> m_EdgesPipeline;
        std::shared_ptr<GraphicsPipeline> m_WeightsPipeline;
        std::shared_ptr<GraphicsPipeline> m_BlendPipeline;
        std::shared_ptr<Shader>           m_EdgesShader;
        std::shared_ptr<Shader>           m_WeightsShader;
        std::shared_ptr<Shader>           m_BlendShader;

        std::unique_ptr<MaterialSMAAEdges>   m_MatEdges;
        std::unique_ptr<MaterialSMAAWeights> m_MatWeights;
        std::unique_ptr<MaterialSMAABlend>   m_MatBlend;

        std::shared_ptr<Image2D> m_AreaTex;
        std::shared_ptr<Image2D> m_SearchTex;
    };
} // namespace Desert::Graphic::System
