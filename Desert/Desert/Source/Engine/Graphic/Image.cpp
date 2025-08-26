#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/RendererAPI.hpp>
#include <Engine/Graphic/Renderer.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>

namespace Desert::Graphic
{

    std::shared_ptr<Image2D> Image2D::Create( const Core::Formats::Image2DSpecification& spec,
                                              const std::unique_ptr<MipMap2DGenerator>&  mipGenerator )
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::None:
                return nullptr;
            case RendererAPIType::Vulkan:
            {

                const auto& image = std::make_shared<API::Vulkan::VulkanImage2D>( spec );
                image->RT_Invalidate();

                if ( spec.Mips > 1 && mipGenerator )
                {
                    mipGenerator->GenerateMips( image );
                }

                return image;
            }
        }
        DESERT_VERIFY( false, "Unknown RenderingAPI" );
    }

    std::shared_ptr<ImageCube> ImageCube::Create( const Core::Formats::ImageCubeSpecification& spec,
                                                  const std::unique_ptr<MipMapCubeGenerator>&  mipGenerator )
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::None:
                return nullptr;
            case RendererAPIType::Vulkan:
            {

                const auto& image = std::make_shared<API::Vulkan::VulkanImageCube>( spec );
                image->RT_Invalidate();

                if ( spec.Mips > 1 && mipGenerator )
                {
                    mipGenerator->GenerateMips( image );
                }

                return image;
            }
        }
        DESERT_VERIFY( false, "Unknown RenderingAPI" );
    }

    std::shared_ptr<Desert::Graphic::ImageCube>
    ImageCube::Copy( const std::shared_ptr<ImageCube>& targetImageCube )
    {
        // TODO!
        const auto& image = std::make_shared<API::Vulkan::VulkanImageCube>(
             *SP_CAST( API::Vulkan::VulkanImageCube, targetImageCube ) );

        return image;
    }

    namespace Utils
    {
        bool IsDepthFormat( Core::Formats::ImageFormat format )
        {
            if ( format == Core::Formats::ImageFormat::DEPTH32F )
                return true;
            if ( format == Core::Formats::ImageFormat::DEPTH24STENCIL8 )
                return true;
            return false;
        }

        bool HasStencilComponent( Core::Formats::ImageFormat format )
        {
            switch ( format )
            {
                case Core::Formats::ImageFormat::DEPTH24STENCIL8:
                    return true;
                default:
                    return false;
            }
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
