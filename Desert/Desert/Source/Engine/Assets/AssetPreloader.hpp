#pragma once

#include "Mesh/MeshAsset.hpp"
#include "Mesh/MaterialAsset.hpp"
#include "Mesh/MeshAssetBundle.hpp"

namespace Desert::Assets
{
    class AssetPreloader
    {
    public:
        explicit AssetPreloader( const std::shared_ptr<AssetManager>& assetManager );

        void                           PreloadAllAssets();
        std::vector<MeshAssetBundle>&& GetMeshAssetBundles()
        {
            return std::move( m_MeshAssetBundle );
        }

    private:
        void PreloadMeshes();
        void ProcessMeshFile( const std::filesystem::path& filePath );

    private:
        std::weak_ptr<AssetManager> m_AssetManager;

        std::vector<MeshAssetBundle> m_MeshAssetBundle;
    };
} // namespace Desert::Assets