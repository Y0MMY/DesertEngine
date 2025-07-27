#pragma once

#include "Common.hpp"
#include "AssetMetadata.hpp"
#include "Mesh/MeshAssetBundle.hpp"

namespace Desert::Assets
{
    class AssetCatalog
    {
    public:
        void RegisterBundles( std::vector<MeshAssetBundle>&& bundles );
        void RegisterMeshMaterial( AssetHandle handle, AssetHandle material );

        const auto& GetAllMeshBundles() const
        {
            return m_AllMeshBundles;
        }

        std::optional<AssetHandle> GetMaterialForMesh( AssetHandle handle ) const;

    private:
        std::vector<MeshAssetBundle>                 m_AllMeshBundles;
        std::unordered_map<AssetHandle, AssetHandle> m_MeshToMaterial;
    };
} // namespace Desert::Assets