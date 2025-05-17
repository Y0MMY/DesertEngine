#include <Engine/Graphic/Renderer.hpp>

#include <Engine/Graphic/RendererContext.hpp>
#include <Engine/Graphic/RendererAPI.hpp>
#include <Engine/Core/EngineContext.h>

#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderer.hpp>

namespace Desert::Graphic
{
    static RendererAPI*                 s_RendererAPI = nullptr;
    static std::shared_ptr<Framebuffer> s_frame;
    static std::shared_ptr<Shader>      s_shader;

    [[nodiscard]] Common::BoolResult Renderer::InitGraphicAPI()
    {
        m_RendererContext =
             RendererContext::Create( EngineContext::GetInstance().GetCurrentPointerToGLFWwinodw() );
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::Vulkan:
            {
                std::static_pointer_cast<Graphic::API::Vulkan::VulkanContext>( m_RendererContext )
                     ->CreateVKInstance();

                s_RendererAPI = new Graphic::API::Vulkan::VulkanRendererAPI;

                break;
            }
        }
        s_RendererAPI->Init();

        return BOOLSUCCESS;
    }

    Common::BoolResult Renderer::Init()
    {
        return InitGraphicAPI();
    }

    [[nodiscard]] Common::BoolResult Renderer::EndFrame()
    {
        return s_RendererAPI->EndFrame();
    }

    [[nodiscard]] Common::BoolResult Renderer::BeginFrame()
    {
        return s_RendererAPI->BeginFrame();
    }

    uint32_t Renderer::GetCurrentFrameIndex()
    {
        return EngineContext::GetInstance().GetCurrentBufferIndex();
    }

    Common::Memory::CommandBuffer& Renderer::GetRenderCommandQueue()
    {
        static Common::Memory::CommandBuffer cmdBuffer;
        return cmdBuffer;
    }

    void Renderer::PresentFinalImage()
    {
        s_RendererAPI->PresentFinalImage();
    }

    void Renderer::SubmitFullscreenQuad( const std::shared_ptr<Pipeline>& pipeline,
                                         const std::shared_ptr<Material>& material )
    {
        s_RendererAPI->SubmitFullscreenQuad( pipeline, material );
    }

    void Renderer::BeginRenderPass( const std::shared_ptr<RenderPass>& renderPass )
    {
        s_RendererAPI->BeginRenderPass( renderPass );
    }

    void Renderer::BeginSwapChainRenderPass()
    {
        s_RendererAPI->BeginSwapChainRenderPass();
    }

    void Renderer::EndRenderPass()
    {
        s_RendererAPI->EndRenderPass();
    }

    void Renderer::ResizeWindowEvent( uint32_t width, uint32_t height,
                                      const std::vector<std::shared_ptr<Framebuffer>>& framebuffers )
    {
        s_RendererAPI->ResizeWindowEvent( width, height, framebuffers );
    }

    std::shared_ptr<Framebuffer> Renderer::GetCompositeFramebuffer()
    {
        return s_RendererAPI->GetCompositeFramebuffer();
    }

    void Renderer::RenderMesh( const std::shared_ptr<Pipeline>& pipeline, const std::shared_ptr<Mesh>& mesh,
                               const glm::mat4& mvp /*TEMP*/ )
    {
        s_RendererAPI->RenderMesh( pipeline, mesh, mvp );
    }

    std::shared_ptr<Desert::Graphic::ImageCube> Renderer::CreateEnvironmentMap( const Common::Filepath& filepath )
    {
        return s_RendererAPI->CreateEnvironmentMap( filepath );
    }
#ifdef DESERT_CONFIG_DEBUG
    [[nodiscard]] Common::BoolResult Renderer::GenerateMipMap( const std::shared_ptr<Image2D>& image )
    {
        return s_RendererAPI->GenerateMipMaps( image );
    }
#endif

} // namespace Desert::Graphic