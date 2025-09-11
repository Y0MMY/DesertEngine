#include "Device.hpp"
#include <Engine/Graphic/RendererAPI.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>

namespace Desert::Engine
{
    std::shared_ptr<Device> Device::Create()
    {
        switch ( Graphic::RendererAPI::GetAPIType() )
        {
            case Graphic::RendererAPIType::None:
                return nullptr;
            case Graphic::RendererAPIType::Vulkan:
            {
                return std::make_shared<Graphic::API::Vulkan::VulkanLogicalDevice>();
            }
        }
        DESERT_VERIFY( false, "Unknown RenderingAPI" );
        return nullptr;
    }

} // namespace Desert::Engine