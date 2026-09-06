#include <Engine/Graphic/RendererContext.hpp>
#include <Engine/Graphic/RendererAPI.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>

namespace Desert::Graphic
{
    std::shared_ptr<RendererContext> RendererContext::Create( const std::shared_ptr<Window>& window )
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::Vulkan:
            {
                return std::make_shared<API::Vulkan::VulkanContext>( window );
            }
            // NAMED RATHER THAN LEFT TO FALL THROUGH. `None` is the enum's zero, not a backend, and the
            // verify below is what answers it — but with the case unwritten this switch also stayed silent
            // the day a SECOND backend is added, which is the one moment a factory needs to complain.
            case RendererAPIType::None:
                break;
        }
        DESERT_VERIFY( false );
        return nullptr;
    }

} // namespace Desert::Graphic