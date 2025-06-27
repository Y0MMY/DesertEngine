#include <Engine/Graphic/FallbackTextures.hpp>
#include <Engine/Graphic/RendererAPI.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanFallbackTextures.hpp>

namespace Desert::Graphic
{
    static std::unique_ptr<FallbackTextures> s_Instance;

    FallbackTextures& FallbackTextures::Get()
    {
        if ( !s_Instance )
        {
            switch ( RendererAPI::GetAPIType() )
            {
                case RendererAPIType::Vulkan:
                    s_Instance = std::make_unique<API::Vulkan::VulkanFallbackTextures>();
                    break;
                case RendererAPIType::None:
                default:
                    DESERT_VERIFY( false, "Unsupported RendererAPI" );
            }
        }
        return *s_Instance;
    }

} // namespace Desert::Graphic