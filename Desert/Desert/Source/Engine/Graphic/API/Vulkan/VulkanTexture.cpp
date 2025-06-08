#include <Engine/Graphic/API/Vulkan/VulkanTexture.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>

#include <Engine/Core/IO/ImageReader.hpp>

namespace Desert::Graphic::API::Vulkan
{
    struct ImageBaseSpec
    {
        std::string                    Tag;
        uint32_t                       Width;
        uint32_t                       Height;
        Core::Formats::ImageFormat     Format;
        uint32_t                       Mips = 1;
        Core::Formats::ImagePixelData  Data;
        Core::Formats::ImageProperties Properties;
    };

    static ImageBaseSpec LoadTexture( const std::filesystem::path& path, bool alpha, bool isCube,
                                      const TextureSpecification& specification )
    {

        bool isHDR = Core::IO::ImageReader::IsHDR( path );

        ImageBaseSpec imageSpecification;
        imageSpecification.Tag = Common::Utils::FileSystem::GetFileName( path );

        if ( isHDR )
        {
            const auto& imageData     = Core::IO::ImageReader::ReadHDR( path );
            imageSpecification.Width  = imageData.Width;
            imageSpecification.Height = imageData.Height;
            imageSpecification.Format = Core::Formats::ImageFormat::RGBA32F;
            imageSpecification.Data   = imageData.Data;
        }
        else
        {
            const auto& imageData     = Core::IO::ImageReader::Read( path, alpha );
            imageSpecification.Width  = imageData.Width;
            imageSpecification.Height = imageData.Height;
            imageSpecification.Format = Core::Formats::ImageFormat::RGBA8F;
            imageSpecification.Data   = imageData.Data;
        }

        imageSpecification.Properties = Core::Formats::Sample;
        imageSpecification.Mips = Utils::CalculateMipCount( imageSpecification.Width, imageSpecification.Height );

        LOG_INFO( "Loading texture {}, alpha channel = {}, HDR = {}",
                  Common::Utils::FileSystem::GetFileName( path ), alpha, isHDR );

        return imageSpecification;
    }

    VulkanTexture2D::VulkanTexture2D( const TextureSpecification&  specification,
                                      const std::filesystem::path& path )
         : m_TexturePath( path ), m_Specification( specification )
    {
    }

    Common::BoolResult VulkanTexture2D::Invalidate()
    {
        const ImageBaseSpec imageBaseSpec = LoadTexture( m_TexturePath, true, false, m_Specification );

        const Core::Formats::Image2DSpecification imageSpec = { .Tag        = imageBaseSpec.Tag,
                                                                .Width      = imageBaseSpec.Width,
                                                                .Height     = imageBaseSpec.Height,
                                                                .Format     = imageBaseSpec.Format,
                                                                .Mips       = imageBaseSpec.Mips,
                                                                .Data       = imageBaseSpec.Data,
                                                                .Usage      = Core::Formats::Image2DUsage::Image2D,
                                                                .Properties = imageBaseSpec.Properties };
        const auto mipGenerator = MipMap2DGenerator::Create( MipGenStrategy::ComputeShader );
        m_Image2D               = Image2D::Create( imageSpec, mipGenerator );

        return Common::MakeSuccess( true ); // TODO
        // return std::static_pointer_cast<Graphic::API::Vulkan::VulkanImage2D>( m_Image2D )->RT_Invalidate();
    }

    VulkanTextureCube::VulkanTextureCube( const TextureSpecification&  specification,
                                          const std::filesystem::path& path )
         : m_TexturePath( path ), m_Specification( specification )
    {
    }

    Common::BoolResult VulkanTextureCube::Invalidate()
    {
        const ImageBaseSpec imageBaseSpec = LoadTexture( m_TexturePath, true, false, m_Specification );

        const Core::Formats::ImageCubeSpecification imageSpec = { .Tag        = imageBaseSpec.Tag,
                                                                  .Width      = imageBaseSpec.Width,
                                                                  .Height     = imageBaseSpec.Height,
                                                                  .Format     = imageBaseSpec.Format,
                                                                  .Mips       = 1u,
                                                                  .Data       = imageBaseSpec.Data,
                                                                  .Properties = imageBaseSpec.Properties };

        const auto mipGenerator = MipMapCubeGenerator::Create( MipGenStrategy::ComputeShader );
        m_ImageCube = ImageCube::Create( imageSpec, mipGenerator);
        return Common::MakeSuccess( true ); // TODO
        // return std::static_pointer_cast<Graphic::API::Vulkan::VulkanImage2D>( m_Image2D )->RT_Invalidate();
    }

} // namespace Desert::Graphic::API::Vulkan