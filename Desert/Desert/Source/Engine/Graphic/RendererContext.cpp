#include <Engine/Graphic/RendererContext.hpp>
#include <Engine/Graphic/RendererAPI.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>

namespace Desert::Graphic
{
    static std::shared_ptr<RendererContext> s_RendererContext = nullptr;

    std::shared_ptr<RendererContext> RendererContext::Create( GLFWwindow* window )
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::Vulkan:
            {

                return std::make_shared<API::Vulkan::VulkanContext>( window );
            }
        }

        DESERT_VERIFY( false );
        return nullptr;
    }

} // namespace Desert::Graphic