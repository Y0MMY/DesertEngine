#pragma once

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Skybox/SkyboxAsset.hpp>
#include <Engine/Graphic/Texture.hpp>
#include <Engine/Graphic/Geometry/Mesh.hpp>
#include <Engine/Graphic/Materials/Skybox/MaterialSkybox.hpp> //TODO: maybe fix?

namespace Desert::Runtime
{
    class ResourceRegistry final
    {
    public:
        void RegisterTexture( const Assets::AssetHandle& handle, std::shared_ptr<Graphic::Texture2D> texture );

        [[nodiscard]] Common::BoolResult
        RegisterSkybox( const std::shared_ptr<Assets::SkyboxAsset>& skyboxAssetAsset );

        [[nodiscard]] Common::BoolResult RegisterMesh( const std::shared_ptr<Assets::MeshAsset>& meshAsset );

        std::shared_ptr<Graphic::Texture2D> GetTexture( const Assets::AssetHandle& handle ) const;
        std::optional<std::shared_ptr<Graphic::MaterialSkybox>>
                              GetSkybox( const Assets::AssetHandle& handle ) const;
        std::shared_ptr<Mesh> GetMesh( const Assets::AssetHandle& handle ) const;

        void Clear();

    private:
        std::unordered_map<Assets::AssetHandle, std::shared_ptr<Graphic::Texture2D>> m_Textures;
        std::unordered_map<Assets::AssetHandle, std::shared_ptr<Graphic::MaterialSkybox>>
             m_Skyboxes; // TODO: maybe fix? bad solution with MaterialSkybox?
        std::unordered_map<Assets::AssetHandle, std::shared_ptr<Mesh>> m_Meshes;
    };
} // namespace Desert::Runtime