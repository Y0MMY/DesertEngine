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

                const auto& image  = std::make_shared<API::Vulkan::VulkanImage2D>( spec );
                const auto  result = image->RT_Invalidate();
                if ( !result.IsSuccess() )
                {
                    // ASKED, NOT ASSUMED — the treatment Image3D::Create below already had, and the
                    // reason it has it applies here word for word: handing back a half-built image
                    // pushes the failure into the first pass that samples it, with no connection to the
                    // allocation that actually failed. `nullptr` is inside this function's declared
                    // contract, not a new outcome: the RendererAPIType::None arm above returns it.
                    LOG_ERROR( "Image2D::Create: image {}x{} failed: {}", spec.Width, spec.Height,
                               result.GetError() );
                    return nullptr;
                }

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

                const auto& image  = std::make_shared<API::Vulkan::VulkanImageCube>( spec );
                const auto  result = image->RT_Invalidate();
                if ( !result.IsSuccess() )
                {
                    LOG_ERROR( "ImageCube::Create: cube '{}' with a {}-texel face failed: {}", spec.Tag,
                               spec.FaceSize, result.GetError() );
                    return nullptr;
                }

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
