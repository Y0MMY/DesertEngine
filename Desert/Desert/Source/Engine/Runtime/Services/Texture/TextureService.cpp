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

    std::string TextureService::GetSourcePath( const Assets::AssetHandle& handle ) const
    {
        if ( auto it = m_TextureAssets.find( handle ); it != m_TextureAssets.end() )
        {
            if ( !it->second->IsReadyForUse() )
                it->second->Load(); // reads the .tex metadata (source path), not pixels
            return it->second->GetSourcePath();
        }
        return {};
    }

    void TextureService::Clear()
    {
        // Was an empty body. Every other service's Clear() drops its maps, and this one is the service
        // that holds the built GPU Texture2Ds — so the one that had to work is the one that did nothing.
        m_Textures.clear();
        m_TextureAssets.clear();
    }

} // namespace Desert::Runtime