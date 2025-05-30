#include <Engine/Graphic/RendererContext.hpp>
#include <Engine/Graphic/RendererAPI.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>

namespace Desert::Graphic
{
    std::unique_ptr<RendererContext> RendererContext::Create( GLFWwindow* window )
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::Vulkan:
            {

                return std::make_unique<API::Vulkan::VulkanContext>( window );
            }
        }

        DESERT_VERIFY( false );
        return nullptr;
    }

} // namespace Desert::Graphic