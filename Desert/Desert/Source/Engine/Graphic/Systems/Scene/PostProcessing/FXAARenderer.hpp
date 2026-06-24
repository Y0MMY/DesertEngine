#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>

#include <Engine/Graphic/Renderer.hpp>

#include <Engine/Graphic/Materials/PostProcessing/MaterialFXAA.hpp>

namespace Desert::Graphic::System
{
    // FXAA post-process pass. Reads the tonemapped image (m_TargetFramebuffer) and writes an
    // anti-aliased copy into its own framebuffer (GetSystemFramebuffer()). Part of the explicit
    // post-process chain, not the render graph.
    class FXAARenderer final : public RenderSystem
    {
    public:
        using RenderSystem::RenderSystem;

        virtual Common::BoolResultStr Initialize() override;
        virtual void                  Shutdown() override {}

        void RegisterPasses( RenderGraphBuilder& builder ) override
        {
        }

        void Execute();
        void Resize( uint32_t width, uint32_t height );

    private:
        void Render();

        std::shared_ptr<GraphicsPipeline> m_Pipeline;
        std::shared_ptr<Shader>           m_Shader;
        std::unique_ptr<MaterialFXAA>     m_MaterialFXAA;
    };
} // namespace Desert::Graphic::System
