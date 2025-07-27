#include "AssetCatalog.hpp"

namespace Desert::Assets
{
    void AssetCatalog::RegisterBundles( std::vector<MeshAssetBundle>&& bundles )
    {
        for ( auto& bundle : bundles )
        {
            m_MeshToMaterial[bundle.MeshMetadata.Handle] = bundle.MaterialMetadata.Handle;
            m_AllMeshBundles.push_back( std::move( bundle ) );
        }
    }

    void AssetCatalog::RegisterMeshMaterial( AssetHandle handle, AssetHandle material )
    {
        // TODO
    }

    std::optional<AssetHandle> AssetCatalog::GetMaterialForMesh( AssetHandle handle ) const
    {
        if ( auto it = m_MeshToMaterial.find( handle ); it != m_MeshToMaterial.end() )
            return it->second;
        return std::nullopt;
    }

} // namespace Desert::Assets