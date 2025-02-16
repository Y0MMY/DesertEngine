#pragma once

#include <Engine/Graphic/Renderer.hpp>

namespace Desert::Graphic
{
    class SceneRenderer final
    {
    public:
        void Init();

        void BeginFrame();
        void EndFrame();

    private:
        /* std::shared_ptr<Graphic::VertexBuffer> m_TESTVertexbuffer;
         std::shared_ptr<Graphic::Pipeline>     m_TESTPipeline;
         std::shared_ptr<Graphic::Framebuffer>  m_TESTFramebuffer;
         std::shared_ptr<Graphic::Shader>       m_TESTShader;
         std::shared_ptr<Graphic::Shader>       m_TESTCompShader;
         std::shared_ptr<Graphic::RenderPass>   m_TESTRenderPass;*/

        std::shared_ptr<Graphic::Pipeline>     m_TESTPipelineSkybox;
        std::shared_ptr<Graphic::Framebuffer>  m_TESTFramebufferSkybox;
        std::shared_ptr<Graphic::Shader>       m_TESTShaderSkybox;
        std::shared_ptr<Graphic::RenderPass>   m_TESTRenderPassSkybox;
        std::shared_ptr<Graphic::VertexBuffer> m_TESTVertexbufferSkybox;
        std::shared_ptr<Graphic::IndexBuffer>  m_TESTIndexbufferSkybox;
        std::shared_ptr<Graphic::Texture>      m_TESTTextureSkybox;
    };
} // namespace Desert::Graphic