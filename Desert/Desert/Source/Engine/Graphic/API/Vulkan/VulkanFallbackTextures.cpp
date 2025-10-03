#include <Engine/Graphic/API/Vulkan/VulkanFallbackTextures.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>

#include <Engine/Core/Formats/ImageFormat.hpp>

namespace Desert::Graphic::API::Vulkan
{

    VulkanFallbackTextures::VulkanFallbackTextures()
    {
        const std::vector<Core::Formats::ImageFormat> formats = { Core::Formats::ImageFormat::RGBA8F,
                                                                  Core::Formats::ImageFormat::RGBA32F };

        for ( auto format : formats )
        {
            CreateFallbackTexture2D( format );
            CreateFallbackTextureCube( format );
        }
    }

    const std::shared_ptr<Image2D>&
    VulkanFallbackTextures::GetFallbackTexture2D( Core::Formats::ImageFormat format ) const
    {
        return m_FallbackTextures2D.at( format );
    }

    const std::shared_ptr<ImageCube>&
    VulkanFallbackTextures::GetFallbackTextureCube( Core::Formats::ImageFormat format ) const
    {
        return m_FallbackTexturesCube.at( format );
    }

    void VulkanFallbackTextures::CreateFallbackTexture2D( Core::Formats::ImageFormat format )
    {
        Core::Formats::Image2DSpecification spec = {
             .Tag        = "VulkanFallbackTextures-2D",
             .Width      = 1,
             .Height     = 1,
             .Format     = format,
             .Mips       = 1,
             .Usage      = Core::Formats::Image2DUsage::Image2D,
             .Properties = Core::Formats::ImageProperties::Sample,
        };

        switch ( format )
        {
            case Core::Formats::ImageFormat::RGBA8F:
            case Core::Formats::ImageFormat::BGRA8F:
                spec.Data = std::vector<unsigned char>{ 255, 255, 255, 255 };
                break;
            case Core::Formats::ImageFormat::RGBA32F:
                spec.Data = std::vector<float>{ 1.0f, 1.0f, 1.0f, 1.0f };
                break;
            default:
                return;
        }

        auto texture = std::make_shared<VulkanImage2D>( spec );
        if ( texture->RT_Invalidate().IsSuccess() )
        {
            m_FallbackTextures2D[format] = texture;
        }
    }

    void VulkanFallbackTextures::CreateFallbackTextureCube( Core::Formats::ImageFormat format )
    {
        Core::Formats::ImageCubeSpecification spec = {
             .Tag        = "VulkanFallbackTextures-Cube",
             .Width      = 4 * 4,
             .Height     = 4 * 3,
             .Format     = format,
             .Mips       = 1,
             .Properties = Core::Formats::ImageProperties::Sample,
        };

        switch ( format )
        {
            case Core::Formats::ImageFormat::RGBA8F:
            case Core::Formats::ImageFormat::BGRA8F:
            {
                spec.Data = std::vector<unsigned char>( spec.Width * spec.Height * 4, 0xFF);
                break;
            }
            case Core::Formats::ImageFormat::RGBA32F:
            {
                std::vector<float> data( spec.Width * spec.Height * 4, 1.0f);
                spec.Data = data;
                break;
            }
            default:
                return;
        }

        auto texture = std::make_shared<VulkanImageCube>( spec );
        if ( texture->RT_Invalidate().IsSuccess() )
        {
            m_FallbackTexturesCube[format] = texture;
        }
    }

    Common::BoolResultStr VulkanFallbackTextures::Release()
    {
        for ( auto& texture2D : m_FallbackTextures2D )
        {
            texture2D.second->Release();
            texture2D.second.reset();
        }

        for ( auto& textureCube : m_FallbackTexturesCube )
        {
            textureCube.second->Release();
            textureCube.second.reset();
        }

        return BOOLSUCCESS;
    }

} // namespace Desert::Graphic::API::Vulkan