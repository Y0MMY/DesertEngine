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

    Assets::AssetHandle MeshService::RegisterProcedural( const std::shared_ptr<Mesh>& mesh )
    {
        Assets::AssetHandle handle{};
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
        auto it = m_Meshes.find( handle );
        return ( it != m_Meshes.end() ) ? it->second.get() : nullptr;
    }

    Assets::MeshAsset* MeshService::GetAsset( const Assets::AssetHandle& handle ) const
    {
        auto it = m_MeshAssets.find( handle );
        return ( it != m_MeshAssets.end() ) ? it->second.get() : nullptr;
    }

    void MeshService::Clear()
    {
        m_Meshes.clear();
        m_MeshAssets.clear();
    }

    std::optional<bool> MeshService::IsSkinned( const Assets::AssetHandle& handle ) const
    {
        auto it = m_Meshes.find( handle );
        return ( it != m_Meshes.end() ) ? std::make_optional( it->second->IsSkinned() ) : std::nullopt;
    }

} // namespace Desert::Runtime
