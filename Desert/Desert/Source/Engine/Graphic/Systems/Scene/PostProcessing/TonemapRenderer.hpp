#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Core/Camera.hpp>

#include <Engine/Graphic/Materials/PostProcessing/MaterialTonemap.hpp>

namespace Desert::Graphic::System
{
    class TonemapRenderer final : public RenderSystem
    {
    public:
        using RenderSystem::RenderSystem;

        virtual Common::BoolResultStr Initialize() override;
        virtual void               Shutdown() override {};

        // Tonemap runs as part of the explicit post-process chain (after the Jump Flood outline),
        // not through the render graph.
        void RegisterPasses( RenderGraphBuilder& builder ) override
        {
        }

        // Reads the configured source framebuffer and writes the tonemapped final image.
        void Execute();

        void Resize( uint32_t width, uint32_t height );

    private:
        void Render();
    private:
        std::shared_ptr<GraphicsPipeline> m_Pipeline;
        std::shared_ptr<Shader>   m_Shader;

        std::unique_ptr<MaterialTonemap> m_MaterialTonemap;
    };
} // namespace Desert::Graphic::System