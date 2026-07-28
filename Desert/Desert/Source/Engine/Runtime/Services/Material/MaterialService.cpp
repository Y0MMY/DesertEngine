#include "MaterialService.hpp"

#include <Engine/Graphic/Materials/MaterialFactory.hpp>
#include <Engine/Graphic/Renderer.hpp>

namespace Desert::Runtime
{
    Common::BoolResultStr MaterialService::Register( const std::shared_ptr<Assets::MaterialAsset>& materialAsset )
    {
        if ( !materialAsset->GetMetadata().IsValid() )
        {
            return Common::MakeError( "Material asset is invalid" );
        }

        auto handle   = materialAsset->GetMetadata().Handle;
        auto material = Graphic::MaterialFactory::CreateMaterial( materialAsset.get() );

        m_Materials[handle]      = material;
        m_MaterialAssets[handle] = materialAsset; // keep the shell too

        m_ExternalToInternal[materialAsset->GetMaterialUUID()] = handle;

        return BOOLSUCCESS;
    }

    Common::BoolResultStr
    MaterialService::RegisterAsset( const std::shared_ptr<Assets::MaterialAsset>& materialAsset )
    {
        if ( !materialAsset->GetMetadata().IsValid() )
        {
            return Common::MakeError( "Material asset is invalid" );
        }
        const auto handle        = materialAsset->GetMetadata().Handle;
        m_MaterialAssets[handle]  = materialAsset; // runtime Material built lazily on first Get
        // The mesh->material link resolves by EXTERNAL id, so the map must exist before any build.
        m_ExternalToInternal[materialAsset->GetMaterialUUID()] = handle;
        return BOOLSUCCESS;
    }

    Graphic::Material* MaterialService::Get( const Assets::AssetHandle& handle ) const
    {
        if ( auto it = m_Materials.find( handle ); it != m_Materials.end() )
            return it->second.get();

        // Lazy build: a shell was registered but the runtime material (with its bound textures) isn't built.
        if ( auto ait = m_MaterialAssets.find( handle ); ait != m_MaterialAssets.end() )
        {
            auto material       = Graphic::MaterialFactory::CreateMaterial( ait->second.get() );
            auto* raw           = material.get();
            m_Materials[handle] = std::move( material );
            return raw;
        }
        return nullptr;
    }

    void MaterialService::Clear()
    {
    }

    void MaterialService::Invalidate( const Assets::AssetHandle& handle )
    {
        auto it = m_Materials.find( handle );
        if ( it == m_Materials.end() )
            return;
        // Keep the material alive until CollectGarbage(): the frame being recorded (and frames
        // in flight) may still reference its descriptor pools — destroying them now invalidates
        // the command buffer (-> device lost).
        m_Graveyard.push_back( std::move( it->second ) );
        m_Materials.erase( it );
    }

    void MaterialService::CollectGarbage()
    {
        if ( m_Graveyard.empty() )
            return;
        // Safe point: no frame is being recorded (caller guarantees frame start) and idle-wait
        // retires every in-flight frame that could reference the dying descriptor pools.
        Graphic::Renderer::GetInstance().WaitDeviceIdle();
        m_Graveyard.clear();
    }

    Graphic::Material* MaterialService::GetByExternalHandle( const Common::UUID& handle ) const
    {
        auto it = m_ExternalToInternal.find( handle );
        if ( it == m_ExternalToInternal.end() )
        {
            return nullptr;
        }

        return Get( it->second );
    }

    Assets::AssetHandle MaterialService::GetAssetHandleByExternal( const Common::UUID& uuid ) const
    {
        auto it = m_ExternalToInternal.find( uuid );
        if ( it == m_ExternalToInternal.end() )
        {
            return Common::UUID::Null();
        }

        return it->second;
    }

} // namespace Desert::Runtime
