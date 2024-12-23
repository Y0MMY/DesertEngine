#include <Engine/Graphic/Framebuffer.hpp>
#include <Engine/Graphic/RendererAPI.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanFramebuffer.hpp>

namespace Desert::Graphic
{
    std::shared_ptr<Framebuffer> Framebuffer::Create( const FramebufferSpecification& spec )
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::None:
                return nullptr;
            case RendererAPIType::Vulkan:
                return std::make_shared<API::Vulkan::VulkanFramebuffer>(spec);
        }
        DESERT_VERIFY( false, "Unknown RenderingAPI" );
        return nullptr;
    }

} // namespace Radiant