#include "TextureService.hpp"

namespace Desert::Runtime
{
    void TextureService::Register( const Assets::AssetHandle& handle, std::shared_ptr<Graphic::Texture2D> texture )
    {
        m_Textures[handle] = std::move( texture );
    }

    std::shared_ptr<Desert::Graphic::Texture2D> TextureService::Get( const Assets::AssetHandle& handle ) const
    {
        auto it = m_Textures.find( handle );
        return ( it != m_Textures.end() ) ? it->second : nullptr;
    }

    void TextureService::Clear()
    {
    }

} // namespace Desert::Runtime