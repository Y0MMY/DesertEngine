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
            CreateFallbackStorageImage2D( format );
            CreateFallbackTexture3D( format );
            CreateFallbackStorageImage3D( format );
        }
    }

    const std::shared_ptr<Image2D>&
    VulkanFallbackTextures::GetFallbackTexture2D( Core::Formats::ImageFormat format ) const
    {
        return m_FallbackTextures2D.at( format );
    }

    const std::shared_ptr<Image2D>&
    VulkanFallbackTextures::GetFallbackStorageImage2D( Core::Formats::ImageFormat format ) const
    {
        return m_FallbackStorageImages2D.at( format );
    }

    const std::shared_ptr<ImageCube>&
    VulkanFallbackTextures::GetFallbackTextureCube( Core::Formats::ImageFormat format ) const
    {
        return m_FallbackTexturesCube.at( format );
    }

    const std::shared_ptr<Image3D>&
    VulkanFallbackTextures::GetFallbackTexture3D( Core::Formats::ImageFormat format ) const
    {
        return m_FallbackTextures3D.at( format );
    }

    const std::shared_ptr<Image3D>&
    VulkanFallbackTextures::GetFallbackStorageImage3D( Core::Formats::ImageFormat format ) const
    {
        return m_FallbackStorageImages3D.at( format );
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

    void VulkanFallbackTextures::CreateFallbackStorageImage2D( Core::Formats::ImageFormat format )
    {
        Core::Formats::Image2DSpecification spec = {
             .Tag        = "VulkanFallbackStorageImage-2D",
             .Width      = 1,
             .Height     = 1,
             .Format     = format,
             .Mips       = 1,
             .Usage      = Core::Formats::Image2DUsage::Image2D,
             .Properties = Core::Formats::ImageProperties::Storage,
        };

        switch ( format )
        {
            case Core::Formats::ImageFormat::RGBA8F:
            case Core::Formats::ImageFormat::BGRA8F:
                spec.Data = std::vector<unsigned char>{ 0, 0, 0, 255 };
                break;
            case Core::Formats::ImageFormat::RGBA32F:
                spec.Data = std::vector<float>{ 0.0f, 0.0f, 0.0f, 1.0f };
                break;
            default:
                return;
        }

        auto texture = std::make_shared<VulkanImage2D>( spec );
        if ( texture->RT_Invalidate().IsSuccess() )
        {
            m_FallbackStorageImages2D[format] = texture;
        }
    }

    // A 1x1x1 volume. Its only job is to be a DEFINED descriptor for a `sampler3D` binding that nothing
    // has bound yet — a binding left unwritten is undefined memory that reads as garbage, and the 2D
    // fallbacks are not substitutable here: a 2D view in a 3D binding samples nonsense without failing.
    // White, matching the 2D sampled fallback, so a missing volume reads as "no attenuation" rather than
    // as a black hole in the middle of an effect.
    void VulkanFallbackTextures::CreateFallbackTexture3D( Core::Formats::ImageFormat format )
    {
        Core::Formats::Image3DSpecification spec = {
             .Tag        = "VulkanFallbackTextures-3D",
             .Width      = 1,
             .Height     = 1,
             .Depth      = 1,
             .Format     = format,
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
                // Only the formats a fallback is actually requested for are built. Anything else would be
                // a lookup that throws out of .at() later, with no clue where it came from — so say it now.
                LOG_ERROR( "VulkanFallbackTextures: no 3D fallback defined for ImageFormat value {}",
                           static_cast<uint32_t>( format ) );
                return;
        }

        auto texture = std::make_shared<VulkanImage3D>( spec );
        if ( texture->RT_Invalidate().IsSuccess() )
        {
            m_FallbackTextures3D[format] = texture;
        }
    }

    void VulkanFallbackTextures::CreateFallbackStorageImage3D( Core::Formats::ImageFormat format )
    {
        Core::Formats::Image3DSpecification spec = {
             .Tag        = "VulkanFallbackStorageImage-3D",
             .Width      = 1,
             .Height     = 1,
             .Depth      = 1,
             .Format     = format,
             .Properties = Core::Formats::ImageProperties::Storage,
        };

        switch ( format )
        {
            case Core::Formats::ImageFormat::RGBA8F:
            case Core::Formats::ImageFormat::BGRA8F:
                spec.Data = std::vector<unsigned char>{ 0, 0, 0, 255 };
                break;
            case Core::Formats::ImageFormat::RGBA32F:
                spec.Data = std::vector<float>{ 0.0f, 0.0f, 0.0f, 1.0f };
                break;
            default:
                LOG_ERROR( "VulkanFallbackTextures: no 3D storage fallback defined for ImageFormat value {}",
                           static_cast<uint32_t>( format ) );
                return;
        }

        auto texture = std::make_shared<VulkanImage3D>( spec );
        if ( texture->RT_Invalidate().IsSuccess() )
        {
            m_FallbackStorageImages3D[format] = texture;
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

        for ( auto& storageImage : m_FallbackStorageImages2D )
        {
            storageImage.second->Release();
            storageImage.second.reset();
        }

        for ( auto& texture3D : m_FallbackTextures3D )
        {
            texture3D.second->Release();
            texture3D.second.reset();
        }

        for ( auto& storageImage : m_FallbackStorageImages3D )
        {
            storageImage.second->Release();
            storageImage.second.reset();
        }

        return BOOLSUCCESS;
    }

} // namespace Desert::Graphic::API::Vulkan