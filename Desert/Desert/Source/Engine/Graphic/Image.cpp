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

    namespace Utils
    {
        bool IsDepthFormat( Core::Formats::ImageFormat format )
        {
            if ( format == Core::Formats::ImageFormat::DEPTH32F )
                return true;
            return false;
        }
    } // namespace Utils

    uint32_t Image::GetBytesPerPixel( const Core::Formats::ImageFormat& format )
    {

        switch ( format )
        {
            case Core::Formats::ImageFormat::RGBA8F:
                return 4; // RGBA = 4 channels, 8 bits each

            case Core::Formats::ImageFormat::RGBA32F:
                return 4 * 4;
        }

        return 0U;
    }

    uint32_t Image::CalculateImageSize( uint32_t width, uint32_t height, const Core::Formats::ImageFormat& format )
    {
        uint32_t pixelCount = width * height;
        return pixelCount * GetBytesPerPixel( format );
    }

} // namespace Desert::Graphic
