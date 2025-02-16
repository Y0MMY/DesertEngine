#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/RendererAPI.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>

namespace Desert::Graphic
{

    std::shared_ptr<Image2D> Image2D::Create( const Core::Formats::ImageSpecification& spec )
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::None:
                return nullptr;
            case RendererAPIType::Vulkan:
                return std::make_shared<API::Vulkan::VulkanImage2D>( spec );
        }
        DESERT_VERIFY( false, "Unknown RenderingAPI" );
    }
} // namespace Desert::Graphic
