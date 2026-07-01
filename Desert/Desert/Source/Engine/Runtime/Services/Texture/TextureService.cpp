#include "TextureService.hpp"

#include <Engine/Graphic/TextureFactory.hpp>

namespace Desert::Runtime
{
    void TextureService::Register( const std::shared_ptr<Assets::TextureAsset>& texture )
    {
        m_Textures[texture->GetHandle()]      = Graphic::TextureFactory::Create2D( texture );
        m_TextureAssets[texture->GetHandle()] = texture; // keep the shell too
    }

    void TextureService::RegisterAsset( const std::shared_ptr<Assets::TextureAsset>& texture )
    {
        if ( texture )
            m_TextureAssets[texture->GetHandle()] = texture; // GPU build deferred to the first Get
    }

    Desert::Graphic::Texture2D* TextureService::Get( const Assets::AssetHandle& handle ) const
    {
        if ( auto it = m_Textures.find( handle ); it != m_Textures.end() )
            return it->second.get();

        // Lazy build: a shell was registered but the GPU texture isn't built yet — build + cache it now.
        if ( auto ait = m_TextureAssets.find( handle ); ait != m_TextureAssets.end() )
        {
            if ( !ait->second->IsReadyForUse() )
                ait->second->Load(); // cheap: reads the .tex metadata (source path), not pixels
            auto  tex = Graphic::TextureFactory::Create2D( ait->second );
            auto* raw = tex.get();
            m_Textures[handle] = std::move( tex );
            return raw;
        }
        return nullptr;
    }

    void TextureService::Clear()
    {
    }

} // namespace Desert::Runtime