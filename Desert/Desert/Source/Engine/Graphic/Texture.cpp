#include <Engine/Graphic/Texture.hpp>
#include <Engine/Graphic/RendererAPI.hpp>

#include <Engine/Core/IO/ImageReader.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::Graphic
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

    Texture2D::Texture2D( const TextureSpecification& specification, const std::filesystem::path& path )
         : m_TexturePath( path ), m_Specification( specification )
    {
    }

    Common::BoolResultStr Texture2D::Invalidate()
    {
        // TODO: check if m_TexturePath exists
        const ImageBaseSpec imageBaseSpec = LoadTexture( m_TexturePath, true, false, m_Specification );

        const Core::Formats::Image2DSpecification imageSpec = { .Tag        = imageBaseSpec.Tag,
                                                                .Width      = imageBaseSpec.Width,
                                                                .Height     = imageBaseSpec.Height,
                                                                .Format     = imageBaseSpec.Format,
                                                                .Mips       = imageBaseSpec.Mips,
                                                                .Data       = imageBaseSpec.Data,
                                                                .Usage      = Core::Formats::Image2DUsage::Image2D,
                                                                .Properties = imageBaseSpec.Properties };
        const auto                                mipGenerator =
             m_Specification.GenerateMips ? MipMap2DGenerator::Create( MipGenStrategy::TransferOps ) : nullptr;
        m_Handle = Runtime::ResourceRegistry::GetImageService()->Register(
             std::move( Image2D::Create( imageSpec, mipGenerator ) ), Runtime::ImageHandle::Type::Image2D );
        return Common::MakeSuccess( true ); // TODO
        // return std::static_pointer_cast<Graphic::API::Vulkan::VulkanImage2D>( m_Image2D )->RT_Invalidate();
    }

    Common::ResultStr<std::shared_ptr<Texture2D>> Texture2D::Create( const TextureSpecification&  specification,
                                                                     const std::filesystem::path& path )
    {
        auto texture =
             std::make_shared<Texture2D>( specification, Common::Constants::Path::TEXTUREDIR_PATH / path );
        auto invResult = texture->Invalidate();
        if ( !invResult.IsSuccess() )
        {
            return Common::MakeError<std::shared_ptr<Texture2D>>( invResult.GetError() );
        }
        return Common::MakeSuccess( texture );
    }

    // ***************************************************************************************************************//

    TextureCube::TextureCube( const TextureSpecification& specification, const std::filesystem::path& path )
         : m_TexturePath( path ), m_Specification( specification )
    {
    }

    Common::BoolResultStr TextureCube::Invalidate()
    {
        const ImageBaseSpec imageBaseSpec = LoadTexture( m_TexturePath, true, false, m_Specification );

        const Core::Formats::ImageCubeSpecification imageSpec = { .Tag        = imageBaseSpec.Tag,
                                                                  .Width      = imageBaseSpec.Width,
                                                                  .Height     = imageBaseSpec.Height,
                                                                  .Format     = imageBaseSpec.Format,
                                                                  .Mips       = 1u,
                                                                  .Data       = imageBaseSpec.Data,
                                                                  .Properties = imageBaseSpec.Properties };

        const auto mipGenerator =
             m_Specification.GenerateMips ? MipMapCubeGenerator::Create( MipGenStrategy::TransferOps ) : nullptr;
        m_Handle = Runtime::ResourceRegistry::GetImageService()->Register(
             std::move( ImageCube::Create( imageSpec, mipGenerator ) ), Runtime::ImageHandle::Type::ImageCube );
        return Common::MakeSuccess( true ); // TODO
        // return std::static_pointer_cast<Graphic::API::Vulkan::VulkanImage2D>( m_Image2D )->RT_Invalidate();
    }

    Common::ResultStr<std::shared_ptr<TextureCube>> TextureCube::Create( const TextureSpecification& specification,
                                                                         const std::filesystem::path& path )
    {
        auto texture =
             std::make_shared<TextureCube>( specification, Common::Constants::Path::TEXTUREDIR_PATH / path );
        auto invResult = texture->Invalidate();
        if ( !invResult.IsSuccess() )
        {
            return Common::MakeError<std::shared_ptr<TextureCube>>( invResult.GetError() );
        }
        return Common::MakeSuccess( texture );
    }

} // namespace Desert::Graphic