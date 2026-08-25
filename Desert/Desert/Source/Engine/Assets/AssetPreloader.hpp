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
        // Cloud noise volumes (`.dcnv`). Scanned so the type asset's slot can offer them by name and so a
        // type that names one finds it already loaded; no GPU work happens here, the renderer uploads.
        void PreloadCloudNoiseVolumes();
        // Cloud types (`.decloudtype`). MUST run after PreloadCloudNoiseVolumes: a type names its noise
        // volume by path and binds it in ResolveDependencies, which the AssetManager calls the moment the
        // type is created — a volume that is not in the manager yet resolves to nothing and the type
        // renders with the default edge instead of its own.
        void PreloadCloudTypes();
        // Sculpted hero-cloud bodies (`.dcmv`). Independent of the two above — a body names no other
        // asset and no other asset names it — so its position in the order is free; it is last because
        // a scene without one still has a sky and this is the stage a project may have nothing in.
        void PreloadCloudModellingVolumes();
        // Painted cloud layouts (`.dclayout`). Independent of the three above — a layout names no other
        // asset and no other asset names it — so its position in the order is free; it is last because a
        // scene without one still has a sky, which is the state every shipped scene is in.
        void PreloadCloudLayouts();

    private:
        std::weak_ptr<AssetManager> m_AssetManager;
    };
} // namespace Desert::Assets