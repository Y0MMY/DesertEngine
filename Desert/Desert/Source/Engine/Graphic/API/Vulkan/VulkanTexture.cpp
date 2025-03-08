#include <Engine/Graphic/API/Vulkan/VulkanTexture.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>

#include <stb_image/stb_image.h>

namespace Desert::Graphic::API::Vulkan
{

    VulkanTexture2D::VulkanTexture2D( const std::filesystem::path& path ) : m_TexturePath( path )
    {
        int width, height, nrChannels;

        Core::Formats::ImageSpecification imageSpec = {};

        if ( stbi_is_hdr( path.string().c_str() ) )
        {
            LOG_INFO( "Loading HDR texture {}, srgb = {}, HDR = {}",
                      Common::Utils::FileSystem::GetFileName( path ), false, true );
            float* data      = stbi_loadf( path.string().c_str(), &width, &height, &nrChannels, STBI_rgb_alpha );
            imageSpec.Data   = (std::byte*)data;
            imageSpec.Format = Core::Formats::ImageFormat::RGBA8F;
        }
        else
        {
            LOG_INFO( "Loading texture {}, srgb = {}, HDR = {}", Common::Utils::FileSystem::GetFileName( path ),
                      true, false );
            unsigned char* data = stbi_load( path.string().c_str(), &width, &height, &nrChannels,
                                             false ? STBI_rgb : STBI_rgb_alpha );
            imageSpec.Data      = (std::byte*)data;
            imageSpec.Format    = Core::Formats::ImageFormat::RGBA8F;
        }

        imageSpec.Width  = static_cast<uint32_t>( width );
        imageSpec.Height = static_cast<uint32_t>( height );
        imageSpec.Usage   = Core::Formats::ImageUsage::Image2D;

        m_Image2D = Image2D::Create( imageSpec );
        std::static_pointer_cast<Graphic::API::Vulkan::VulkanImage2D>( m_Image2D )->RT_Invalidate();
    }

    VulkanTextureCube::VulkanTextureCube( const std::filesystem::path& path )
    {
        int width, height, nrChannels;

        Core::Formats::ImageSpecification imageSpec = {};

        if ( stbi_is_hdr( path.string().c_str() ) )
        {
            LOG_INFO( "Loading HDR CUBE texture {}, srgb = {}, HDR = {}",
                      Common::Utils::FileSystem::GetFileName( path ), false, true );
            float* data      = stbi_loadf( path.string().c_str(), &width, &height, &nrChannels, STBI_rgb_alpha);
            imageSpec.Data   = (std::byte*)data;
            imageSpec.Format = Core::Formats::ImageFormat::RGBA8F;
        }
        else
        {
            LOG_INFO( "Loading CUBE texture {}, srgb = {}, HDR = {}",
                      Common::Utils::FileSystem::GetFileName( path ), true, false );
            unsigned char* data = stbi_load( path.string().c_str(), &width, &height, &nrChannels, STBI_rgb_alpha );
            imageSpec.Data      = (std::byte*)data;
            imageSpec.Format    = Core::Formats::ImageFormat::RGBA8F;
        }

        imageSpec.Width  = static_cast<uint32_t>( width );
        imageSpec.Height = static_cast<uint32_t>( height );
        imageSpec.Usage   = Core::Formats::ImageUsage::ImageCube;

        m_Image2D = Image2D::Create( imageSpec );
        std::static_pointer_cast<Graphic::API::Vulkan::VulkanImage2D>( m_Image2D )->RT_Invalidate();
    }

} // namespace Desert::Graphic::API::Vulkan