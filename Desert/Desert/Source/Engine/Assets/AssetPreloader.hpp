#pragma once

#include "Mesh/MeshAsset.hpp"
#include "Mesh/MaterialAsset.hpp"
#include "Skybox/SkyboxAsset.hpp"

#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::Assets
{
    class AssetPreloader
    {
    public:
        explicit AssetPreloader( const std::shared_ptr<AssetManager>&              assetManager,
                                 const std::shared_ptr<Runtime::ResourceRegistry>& resourceRegistry );

        void PreloadAllAssets();

    private:
        void PreloadMeshes();
        void PreloadSkyboxes();

    private:
        std::weak_ptr<AssetManager>              m_AssetManager;
        std::weak_ptr<Runtime::ResourceRegistry> m_ResourceRegistry;
    };
} // namespace Desert::Assets