#include <Engine/Graphic/API/Vulkan/VulkanTexture.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>

#include <Engine/Core/IO/ImageReader.hpp>

namespace Desert::Graphic::API::Vulkan
{

    static Core::Formats::ImageSpecification LoadTexture( const std::filesystem::path& path, bool alpha,
                                                          bool isCube )
    {

        bool isHDR = Core::IO::ImageReader::IsHDR( path );

        Core::Formats::ImageSpecification imageSpecification;

        if ( isHDR )
        {
            const auto& imageData      = Core::IO::ImageReader::ReadHDR( path );
            imageSpecification.Width   = imageData.Width;
            imageSpecification.Height  = imageData.Height;
            imageSpecification.Format  = Core::Formats::ImageFormat::RGBA32F;
            imageSpecification.Data = imageData.Data;
        }
        else
        {
            const auto& imageData      = Core::IO::ImageReader::Read( path, alpha );
            imageSpecification.Width   = imageData.Width;
            imageSpecification.Height  = imageData.Height;
            imageSpecification.Format  = Core::Formats::ImageFormat::RGBA8F;
            imageSpecification.Data = imageData.Data;
        }

        if ( isCube )
        {
            imageSpecification.Usage = Core::Formats::ImageUsage::ImageCube;
        }

        LOG_INFO( "Loading texture {}, alpha channel = {}, HDR = {}",
                  Common::Utils::FileSystem::GetFileName( path ), alpha, isHDR );

        return imageSpecification;
    }

    VulkanTexture2D::VulkanTexture2D( const std::filesystem::path& path ) : m_TexturePath( path )
    {
        Core::Formats::ImageSpecification imageSpec = LoadTexture( path, true, false );

        m_Image2D = Image2D::Create( imageSpec );
        std::static_pointer_cast<Graphic::API::Vulkan::VulkanImage2D>( m_Image2D )->RT_Invalidate();
    }

    VulkanTextureCube::VulkanTextureCube( const std::filesystem::path& path )
    {
        Core::Formats::ImageSpecification imageSpec = LoadTexture( path, true, true );

        m_Image2D = Image2D::Create( imageSpec );
        std::static_pointer_cast<Graphic::API::Vulkan::VulkanImage2D>( m_Image2D )->RT_Invalidate();
    }
} // namespace Desert::Graphic::API::Vulkan