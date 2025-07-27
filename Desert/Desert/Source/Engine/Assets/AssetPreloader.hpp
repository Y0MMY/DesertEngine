#pragma once

#include "Mesh/MeshAsset.hpp"
#include "Mesh/MaterialAsset.hpp"

namespace Desert::Assets
{
    class AssetPreloader
    {
    public:
        explicit AssetPreloader( const std::shared_ptr<AssetManager>& assetManager );

        void PreloadAllAssets();

    private:
        void PreloadMeshes();
        void ProcessMeshFile( const std::filesystem::path& filePath );

    private:
        std::weak_ptr<AssetManager> m_AssetManager;
    };
} // namespace Desert::Assets