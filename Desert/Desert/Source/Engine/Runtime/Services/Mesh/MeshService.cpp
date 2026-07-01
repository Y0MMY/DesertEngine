#include "MeshService.hpp"

#include <Engine/Geometry/MeshFactory.hpp>

namespace Desert::Runtime
{
    Common::BoolResultStr MeshService::Register( const std::shared_ptr<Assets::MeshAsset>& meshAsset )
    {
        if ( !meshAsset->GetMetadata().IsValid() )
        {
            return Common::MakeError( "Mesh asset is invalid" );
        }

        const auto handle = meshAsset->GetMetadata().Handle;
        m_Meshes[handle] = Graphic::MeshFactory::Create( meshAsset );
        m_MeshAssets[handle] = meshAsset;

        return BOOLSUCCESS;
    }

    Common::BoolResultStr MeshService::RegisterAsset( const std::shared_ptr<Assets::MeshAsset>& meshAsset )
    {
        if ( !meshAsset )
            return Common::MakeError( "Mesh asset is null" );
        // Handle is path-derived in the ctor, so a not-yet-loaded shell is keyed correctly. The .stmesh parse
        // + GPU build are deferred to the first Get/GetAsset.
        m_MeshAssets[meshAsset->GetMetadata().Handle] = meshAsset;
        return BOOLSUCCESS;
    }

    Assets::AssetHandle MeshService::RegisterProcedural( const std::shared_ptr<Mesh>& mesh )
    {
        // Procedural meshes have no source path, so mint a fresh random id (the default handle is now Null).
        Assets::AssetHandle handle = Assets::AssetHandle::Generate();
        // Build the GPU vertex/index buffers — the mesh ctor doesn't (asset meshes get this via
        // MeshFactory::Create, and the ECS primitive path Invalidates explicitly). Without this a builtin
        // procedural mesh (e.g. the Cube) has no buffers and renders nothing.
        if ( mesh )
            mesh->Invalidate();
        m_Meshes[handle] = mesh;
        return handle;
    }

    Desert::Mesh* MeshService::Get( const Assets::AssetHandle& handle ) const
    {
        if ( auto it = m_Meshes.find( handle ); it != m_Meshes.end() )
            return it->second.get();

        // Lazy build: a shell was registered — parse the .stmesh (if needed) + build the GPU mesh now.
        if ( auto ait = m_MeshAssets.find( handle ); ait != m_MeshAssets.end() )
        {
            if ( !ait->second->IsReadyForUse() )
                ait->second->Load();
            auto  mesh = Graphic::MeshFactory::Create( ait->second );
            auto* raw  = mesh.get();
            // Don't cache a FAILED build (e.g. a skinned mesh whose skeleton dependency wasn't resolved yet) —
            // otherwise the null is sticky and the mesh can never recover once the dependency is in place.
            if ( raw )
                m_Meshes[handle] = std::move( mesh );
            return raw;
        }
        return nullptr;
    }

    Assets::MeshAsset* MeshService::GetAsset( const Assets::AssetHandle& handle ) const
    {
        auto it = m_MeshAssets.find( handle );
        if ( it == m_MeshAssets.end() )
            return nullptr;
        // Ensure the payload is parsed before callers read submeshes / material handles (lazy shells).
        if ( !it->second->IsReadyForUse() )
            it->second->Load();
        return it->second.get();
    }

    void MeshService::Clear()
    {
        m_Meshes.clear();
        m_MeshAssets.clear();
    }

    std::optional<bool> MeshService::IsSkinned( const Assets::AssetHandle& handle ) const
    {
        // Cheap query: the concrete asset subclass (Static vs Skinned, chosen by extension at registration)
        // encodes skinned-ness via a virtual constant — no need to parse the .stmesh or build the GPU mesh.
        // This must stay cheap: UI (e.g. the mesh selector popup) calls it for EVERY mesh asset every frame.
        // Building here would force a synchronous load+GPU-build of every mesh in the project → multi-second
        // freeze that looks like a hang.
        if ( auto ait = m_MeshAssets.find( handle ); ait != m_MeshAssets.end() )
            return std::make_optional( ait->second->IsSkinned() );

        // Procedural meshes have no asset shell — fall back to the already-built runtime mesh if present.
        if ( auto it = m_Meshes.find( handle ); it != m_Meshes.end() )
            return std::make_optional( it->second->IsSkinned() );

        return std::nullopt;
    }

} // namespace Desert::Runtime
