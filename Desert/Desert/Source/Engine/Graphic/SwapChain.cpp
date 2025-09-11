#include "SwapChain.hpp"

#include <Engine/Graphic/RendererAPI.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanSwapChain.hpp>

namespace Desert::Graphic
{
    std::shared_ptr<SwapChain> SwapChain::Create( const GLFWwindow* window )
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::None:
                return nullptr;
            case RendererAPIType::Vulkan:
            {
                return std::make_shared<API::Vulkan::VulkanSwapChain>( window );
            }
        }
        DESERT_VERIFY( false, "Unknown RenderingAPI" );
        return nullptr;
    }
} // namespace Desert::Graphic