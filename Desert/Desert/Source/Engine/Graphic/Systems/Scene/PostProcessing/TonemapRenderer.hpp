#pragma once

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Core/Camera.hpp>

#include <Engine/Graphic/Models/MeshRenderData.hpp>

namespace Desert::Graphic::System
{
    class TonemapRenderer
    {
    public:
        Common::BoolResult Init( const uint32_t width, const uint32_t height );
        void               Shutdown();

        void Process( const std::shared_ptr<Framebuffer>& inputFramebuffer );

        std::shared_ptr<Image2D> GetOutputImage() const
        {
            return m_Framebuffer->GetColorAttachmentImage();
        }

    private:
        std::shared_ptr<Framebuffer> m_Framebuffer;
        std::shared_ptr<RenderPass>  m_RenderPass;
        std::shared_ptr<Pipeline>    m_Pipeline;
        std::shared_ptr<Shader>      m_Shader;
    };
} // namespace Desert::Graphic::System