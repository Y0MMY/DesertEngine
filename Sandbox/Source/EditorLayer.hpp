#pragma once

#include <Engine/Desert.hpp>

namespace Desert
{
    class EditorLayer : public Common::Layer
    {
    public:
        EditorLayer( const std::string& layerName );

        virtual void OnAttach() override;
        virtual void OnDetach() override
        {
        }
        virtual void OnUpdate( Common::Timestep ts ) override;

    private:
        std::shared_ptr<Graphic::VertexBuffer> m_Vertexbuffer;
        std::shared_ptr<Graphic::Pipeline>     m_Pipeline;
        std::shared_ptr<Graphic::Framebuffer>  m_Framebuffer;
        std::shared_ptr<Graphic::Shader>       m_Shader;
        std::shared_ptr<Graphic::RenderPass>   m_RenderPass;
    };
} // namespace Desert