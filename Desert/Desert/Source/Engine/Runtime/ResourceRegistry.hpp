#pragma once

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Graphic/Texture.hpp>
#include <Engine/Graphic/Geometry/Mesh.hpp>
#include <Engine/Graphic/Environment/SceneEnvironment.hpp>

namespace Desert::Runtime
{
    class ResourceRegistry final
    {
    public:
        void RegisterTexture( const Assets::AssetHandle& handle, std::shared_ptr<Graphic::Texture2D> texture );
        void RegisterSkybox( const Assets::AssetHandle& handle, const Graphic::Environment& skybox );
        [[nodiscard]] Common::BoolResult RegisterMesh( const std::shared_ptr<Assets::MeshAsset>& meshAsset );

        std::shared_ptr<Graphic::Texture2D> GetTexture( const Assets::AssetHandle& handle ) const;
        std::optional<Graphic::Environment> GetSkybox( const Assets::AssetHandle& handle ) const;
        std::shared_ptr<Mesh>               GetMesh( const Assets::AssetHandle& handle ) const;

        void Clear();

    private:
        std::unordered_map<Assets::AssetHandle, std::shared_ptr<Graphic::Texture2D>> m_Textures;
        std::unordered_map<Assets::AssetHandle, Graphic::Environment>                m_Skyboxes;
        std::unordered_map<Assets::AssetHandle, std::shared_ptr<Mesh>>               m_Meshes;
    };
} // namespace Desert::Runtime