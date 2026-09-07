#include "TextureService.hpp"

#include <Engine/Graphic/TextureFactory.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::Runtime
{
    namespace
    {
        // SAY WHOSE THE IMAGE IS, AS SOON AS IT EXISTS. A `Texture2D` is a thin wrapper around an
        // ImageHandle, so the row that costs device memory is the Image's, not the texture's — and until
        // somebody names the asset behind it the ledger correctly reports it as unclaimed. This is the one
        // place that knows both halves. See Engine/Graphic/ResourceLedger.hpp.
        void ClaimTextureImage( const std::shared_ptr<Graphic::Texture2D>& texture,
                                const Assets::AssetHandle&                 asset )
        {
            if ( !texture )
                return;
            if ( auto* image = ResourceRegistry::GetImageService()->Resolve( texture->GetImageHandle() ) )
                image->ClaimOwnership( Graphic::ResourceOwner::AssetService, asset );
        }
    } // namespace

    void TextureService::Register( const std::shared_ptr<Assets::TextureAsset>& texture )
    {
        m_Textures[texture->GetHandle()]      = Graphic::TextureFactory::Create2D( texture );
        m_TextureAssets[texture->GetHandle()] = texture; // keep the shell too
        ClaimTextureImage( m_Textures[texture->GetHandle()], texture->GetHandle() );
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
            ClaimTextureImage( m_Textures[handle], handle );
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