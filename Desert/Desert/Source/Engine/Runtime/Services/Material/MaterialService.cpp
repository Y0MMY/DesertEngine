#include "MaterialService.hpp"

#include <Engine/Assets/Mesh/SurfaceMaterialAsset.hpp>
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
        // Material-instance assets resolve through the parent chain to the BASE material: an
        // instance never builds a runtime Material of its own (its overrides live on the
        // per-entity MaterialInstance, see CreateRuntimeInstance). Depth-capped cycle guard.
        Assets::AssetHandle current = handle;
        for ( int depth = 0; depth < 8; ++depth )
        {
            if ( auto ait = m_MaterialAssets.find( current ); ait != m_MaterialAssets.end() )
            {
                if ( auto* surf = dynamic_cast<Assets::SurfaceMaterialAsset*>( ait->second.get() );
                     surf && surf->Data().IsInstance() )
                {
                    const auto parent = GetAssetHandleByExternal( *surf->Data().ParentMaterialId );
                    if ( parent.IsNull() || parent == current )
                        return nullptr;
                    current = parent;
                    continue;
                }
            }
            break;
        }

        if ( auto it = m_Materials.find( current ); it != m_Materials.end() )
            return it->second.get();

        // Lazy build: a shell was registered but the runtime material (with its bound textures) isn't built.
        if ( auto ait = m_MaterialAssets.find( current ); ait != m_MaterialAssets.end() )
        {
            auto material        = Graphic::MaterialFactory::CreateMaterial( ait->second.get() );
            auto* raw            = material.get();
            m_Materials[current] = std::move( material );
            return raw;
        }
        return nullptr;
    }

    Graphic::MaterialInstancePtr MaterialService::CreateRuntimeInstance( const Assets::AssetHandle& handle ) const
    {
        // Collect the instance chain child -> base (depth-capped cycle guard), then create one
        // runtime instance of the base material and apply overrides base-first so the NEAREST
        // (childmost) override wins.
        std::vector<const Assets::SurfaceMaterialAsset*> chain;
        Assets::AssetHandle                              current = handle;
        for ( int depth = 0; depth < 8; ++depth )
        {
            auto ait = m_MaterialAssets.find( current );
            if ( ait == m_MaterialAssets.end() )
                break;
            auto* surf = dynamic_cast<Assets::SurfaceMaterialAsset*>( ait->second.get() );
            if ( !surf || !surf->Data().IsInstance() )
                break;
            chain.push_back( surf );
            const auto parent = GetAssetHandleByExternal( *surf->Data().ParentMaterialId );
            if ( parent.IsNull() || parent == current )
                break;
            current = parent;
        }

        auto* base = Get( current );
        if ( !base )
            return nullptr;

        auto instance = base->CreateInstance();
        for ( auto it = chain.rbegin(); it != chain.rend(); ++it )
            for ( const auto& p : ( *it )->Data().Params )
                instance->SetParamFromVec4( p.Name, p.Value );
        return instance;
    }

    bool MaterialService::ResolveOverrides( const Assets::AssetHandle&  handle,
                                            Graphic::MaterialOverrides& out ) const
    {
        // Same walk as CreateRuntimeInstance: collect the instance chain child -> base (depth-capped
        // cycle guard), then append base-first so the childmost override lands last and wins.
        std::vector<const Assets::SurfaceMaterialAsset*> chain;
        Assets::AssetHandle                              current = handle;
        for ( int depth = 0; depth < 8; ++depth )
        {
            auto ait = m_MaterialAssets.find( current );
            if ( ait == m_MaterialAssets.end() )
                break;
            auto* surf = dynamic_cast<Assets::SurfaceMaterialAsset*>( ait->second.get() );
            if ( !surf || !surf->Data().IsInstance() )
                break;
            chain.push_back( surf );
            const auto parent = GetAssetHandleByExternal( *surf->Data().ParentMaterialId );
            if ( parent.IsNull() || parent == current )
                break;
            current = parent;
        }

        auto baseIt = m_MaterialAssets.find( current );
        if ( baseIt == m_MaterialAssets.end() )
            return false;
        auto* base = dynamic_cast<Assets::SurfaceMaterialAsset*>( baseIt->second.get() );
        if ( !base )
            return false;

        // Unlike CreateRuntimeInstance this carries TEXTURES as well as params. It can: the consumer binds
        // them onto its own material by sampler name, so there is no per-instance descriptor set to need.
        const auto append = [&out]( const Assets::MaterialData& data )
        {
            for ( const auto& p : data.Params )
                out.Params.emplace_back( p.Name, p.Value );
            for ( const auto& t : data.Textures )
                out.Textures.emplace_back( t.Name, t.TextureHandle );
        };

        append( base->Data() );
        for ( auto it = chain.rbegin(); it != chain.rend(); ++it )
            append( ( *it )->Data() );
        return true;
    }

    void MaterialService::Clear()
    {
        // Was an empty body. The graveyard goes with the rest: at shutdown there is no next frame to
        // collect it, and its materials own descriptor pools that must not outlive the device.
        m_Materials.clear();
        m_MaterialAssets.clear();
        m_ExternalToInternal.clear();
        m_Graveyard.clear();
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
        ++m_InvalidationVersion; // cached instance sets rebuild on their next system tick
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
        if ( it != m_ExternalToInternal.end() )
            return it->second;

        // Identity fallback: file materials ADOPT their in-file MaterialId as the asset handle,
        // so for them external id == internal handle. This makes resolution independent of the
        // order the external->internal map fills in (the map stays authoritative for imported
        // materials whose ids genuinely diverge).
        const Assets::AssetHandle asHandle{ uuid };
        if ( m_MaterialAssets.count( asHandle ) || m_Materials.count( asHandle ) )
            return asHandle;

        return Common::UUID::Null();
    }

} // namespace Desert::Runtime
