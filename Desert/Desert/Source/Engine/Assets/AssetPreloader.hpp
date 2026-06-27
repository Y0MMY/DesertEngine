#pragma once

#include "Mesh/MeshAsset.hpp"
#include "Mesh/PBRMaterialAsset.hpp"
#include "Skybox/SkyboxAsset.hpp"

#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::Assets
{
    class AssetPreloader
    {
    public:
        explicit AssetPreloader( const std::shared_ptr<AssetManager>& assetManager );

        void PreloadAllAssets();

        // Re-process + re-register cooked meshes/textures/materials (Rebuild Cooked Assets). Re-registering
        // reloads texture pixels from source and rebuilds runtime materials; callers must idle the GPU
        // first and clear cached per-entity material instances after. Skips shaders/skyboxes (no IBL re-bake).
        void ReloadCooked();

    private:
        void PreloadMeshes();
        void PreloadSkyboxes();
        void PreloadShaders();

    private:
        std::weak_ptr<AssetManager> m_AssetManager;
    };
} // namespace Desert::Assets