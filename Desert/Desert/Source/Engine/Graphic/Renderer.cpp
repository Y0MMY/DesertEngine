#include <Engine/Graphic/Renderer.hpp>

#include <Engine/Graphic/RendererContext.hpp>
#include <Engine/Graphic/RendererAPI.hpp>
#include <Engine/Core/EngineContext.h>

#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderer.hpp>

namespace Desert::Graphic
{
    static RendererAPI* s_RendererAPI = nullptr;

    void Renderer::InitGraphicAPI()
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
    }

    Common::BoolResult Renderer::Init()
    {
        InitGraphicAPI();



        return Common::MakeSuccess(true);
    }

    void Renderer::ClearImage()
    {
        s_RendererAPI->ClearImage();
    }

    void Renderer::EndFrame()
    {
        s_RendererAPI->EndFrame();
    }

    void Renderer::BeginFrame()
    {
        s_RendererAPI->BeginFrame();
    }

    uint32_t Renderer::GetCurrentFrameIndex()
    {
        return  EngineContext::GetInstance().GetCurrentBufferIndex();
    }

} // namespace Desert::Graphic