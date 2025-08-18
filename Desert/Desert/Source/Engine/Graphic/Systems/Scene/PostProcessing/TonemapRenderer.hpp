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

        virtual Common::BoolResult Initialize( const uint32_t width, const uint32_t height ) override;
        virtual void               Shutdown() override {};

        virtual void ProcessSystem() override;

    private:
        std::shared_ptr<RenderPass>  m_RenderPass;
        std::shared_ptr<Pipeline>    m_Pipeline;
        std::shared_ptr<Shader>      m_Shader;

        std::unique_ptr<MaterialTonemap> m_MaterialTonemap;
    };
} // namespace Desert::Graphic::System