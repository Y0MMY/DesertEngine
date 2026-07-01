#pragma once

#include <Engine/Geometry/Mesh.hpp>

#include <Engine/Assets/Mesh/MeshAsset.hpp>

namespace Desert::Runtime
{
    class MeshService
    {
    public:
        // Eager: parse + build the GPU mesh now.
        Common::BoolResultStr Register( const std::shared_ptr<Assets::MeshAsset>& meshAsset );
        // Lazy: register the asset SHELL only — the .stmesh parse + GPU build are deferred to the first Get.
        // The shell must already carry its path-derived handle (set in the mesh-asset ctor).
        Common::BoolResultStr RegisterAsset( const std::shared_ptr<Assets::MeshAsset>& meshAsset );
        Assets::AssetHandle   RegisterProcedural( const std::shared_ptr<Mesh>& mesh );

        Mesh*                 Get( const Assets::AssetHandle& handle ) const; // builds-on-miss from a shell
        Assets::MeshAsset*    GetAsset( const Assets::AssetHandle& handle ) const;

        void                  Clear();
        std::optional<bool>   IsSkinned( const Assets::AssetHandle& handle ) const;

    private:
        mutable std::unordered_map<Assets::AssetHandle, std::shared_ptr<Mesh>>      m_Meshes;
        std::unordered_map<Assets::AssetHandle, std::shared_ptr<Assets::MeshAsset>> m_MeshAssets;
    };
} // namespace Desert::Runtime