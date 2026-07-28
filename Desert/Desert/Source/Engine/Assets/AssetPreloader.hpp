#pragma once

#include "Mesh/MeshAsset.hpp"
#include "Mesh/SurfaceMaterialAsset.hpp"
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

        // Individual stages — public so the editor's staged startup loader can run them one per frame
        // behind a progress overlay. ORDER MATTERS: shaders must be loaded before any render system is
        // constructed (default PBR materials resolve their shader in the constructor).
        void PreloadMeshes();
        void PreloadSkyboxes();
        void PreloadShaders();

    private:
        std::weak_ptr<AssetManager> m_AssetManager;
    };
} // namespace Desert::Assets