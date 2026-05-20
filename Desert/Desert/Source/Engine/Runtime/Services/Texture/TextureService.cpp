#include "TextureService.hpp"

#include <Engine/Graphic/TextureFactory.hpp>

namespace Desert::Runtime
{
    void TextureService::Register( const std::shared_ptr<Assets::TextureAsset>& texture )
    {
        m_Textures[texture->GetHandle()] = Graphic::TextureFactory::Create2D( texture );
    }

    Desert::Graphic::Texture2D* TextureService::Get( const Assets::AssetHandle& handle ) const
    {
        auto it = m_Textures.find( handle );
        return ( it != m_Textures.end() ) ? it->second.get() : nullptr;
    }

    void TextureService::Clear()
    {
    }

} // namespace Desert::Runtime