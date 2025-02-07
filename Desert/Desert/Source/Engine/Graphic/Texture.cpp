#include <Engine/Graphic/Texture.hpp>
#include <Engine/Graphic/RendererAPI.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanTexture.hpp>

namespace Desert::Graphic
{

    std::shared_ptr<Texture2D> Texture2D::Create( const std::filesystem::path& path )
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::None:
                return nullptr;
            case RendererAPIType::Vulkan:
                return std::make_shared<API::Vulkan::VulkanTexture2D>( Common::Constants::Path::TEXTUREDIR_PATH /
                                                                       path );
        }
        DESERT_VERIFY( false, "Unknown RenderingAPI" );
    }

} // namespace Desert::Graphic