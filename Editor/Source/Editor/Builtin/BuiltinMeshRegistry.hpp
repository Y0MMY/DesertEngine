#pragma once

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Mesh/StaticMeshAsset.hpp>
#include <Common/Core/UUID.hpp>

namespace Desert::Editor
{
    enum class BuiltinMeshType : uint8_t
    {
        Cube,
        Sphere,
        Plane
    };

    class BuiltinMeshRegistry
    {
    public:
        static void Init( const std::shared_ptr<Assets::AssetManager>& assetManager );

        static Assets::AssetHandle Get( BuiltinMeshType type );

        static std::vector<Assets::AssetHandle> GetDefaultMaterials( BuiltinMeshType type );

    private:
        static void RegisterCube( const std::shared_ptr<Assets::AssetManager>& assetManager );
        static void RegisterPlane( const std::shared_ptr<Assets::AssetManager>& assetManager );
        static void RegisterSphere( const std::shared_ptr<Assets::AssetManager>& assetManager );

        static Assets::AssetHandle
        CreateDefaultMaterial( const std::shared_ptr<Assets::AssetManager>& assetManager );

    private:
        static std::unordered_map<BuiltinMeshType, Assets::AssetHandle>              s_BuiltinMeshes;
        static std::unordered_map<BuiltinMeshType, std::vector<Assets::AssetHandle>> s_BuiltinDefaultMaterials;
    };
} // namespace Desert::Editor