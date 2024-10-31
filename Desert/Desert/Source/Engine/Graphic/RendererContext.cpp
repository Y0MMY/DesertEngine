#include <Engine/Graphic/RendererContext.hpp>
#include <Engine/Graphic/RendererAPI.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>

namespace Desert::Graphic
{
    std::shared_ptr<RendererContext> RendererContext::Create(GLFWwindow * window)
    {
        switch ( RendererAPI::GetAPI() )
        {
            case RendererAPIType::Vulkan:
            {
               // return std::make_shared<API::Vulkan::VulkanContext>::Create( window );
            }
        }

        DESERT_VERIFY( false );
        return nullptr;
    }
} // namespace Desert::Graphic