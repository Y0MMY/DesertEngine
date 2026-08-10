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

    std::shared_ptr<Image3D> Image3D::Create( const Core::Formats::Image3DSpecification& spec )
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::None:
                return nullptr;
            case RendererAPIType::Vulkan:
            {
                const auto& image  = std::make_shared<API::Vulkan::VulkanImage3D>( spec );
                const auto  result = image->RT_Invalidate();
                if ( !result.IsSuccess() )
                {
                    // Handing back a half-built volume would push the failure into the first dispatch
                    // that binds it, with no connection to the allocation that actually failed.
                    LOG_ERROR( "Image3D::Create: volume '{}' {}x{}x{} failed: {}", spec.Tag, spec.Width,
                               spec.Height, spec.Depth, result.GetError() );
                    return nullptr;
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

} // namespace Desert::Graphic
