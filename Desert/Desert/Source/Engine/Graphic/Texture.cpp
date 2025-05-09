#include <Engine/Graphic/Texture.hpp>
#include <Engine/Graphic/RendererAPI.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanTexture.hpp>

namespace Desert::Graphic
{

    std::shared_ptr<Texture2D> Texture2D::Create( const TextureSpecification&  specification,
                                                  const std::filesystem::path& path )
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::None:
                return nullptr;
            case RendererAPIType::Vulkan:
                return std::make_shared<API::Vulkan::VulkanTexture2D>(
                     specification, Common::Constants::Path::TEXTUREDIR_PATH / path );
        }
        DESERT_VERIFY( false, "Unknown RendererAPI" );
    }

    std::shared_ptr<TextureCube> TextureCube::Create( const TextureSpecification&  specification,
                                                      const std::filesystem::path& path )
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::None:
                return nullptr;
            case RendererAPIType::Vulkan:
                return std::make_shared<API::Vulkan::VulkanTextureCube>(
                     specification, Common::Constants::Path::TEXTUREDIRENV_PATH / path );
        }
        DESERT_VERIFY( false, "Unknown RendererAPI" );
    }

} // namespace Desert::Graphic