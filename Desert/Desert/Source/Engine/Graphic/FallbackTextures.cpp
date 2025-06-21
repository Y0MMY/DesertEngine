#include <Engine/Graphic/FallbackTextures.hpp>
#include <Engine/Graphic/RendererAPI.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanFallbackTextures.hpp>

namespace Desert::Graphic
{
    std::shared_ptr<FallbackTextures> FallbackTextures::Create()
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::None:
                return nullptr;
            case RendererAPIType::Vulkan:
            {
                return std::make_shared<API::Vulkan::VulkanFallbackTextures>();
            }
        }
        DESERT_VERIFY( false, "Unknown RendererAPI" );
        return nullptr;
    }

} // namespace Desert::Graphic