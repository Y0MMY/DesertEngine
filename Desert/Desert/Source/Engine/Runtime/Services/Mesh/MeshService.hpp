#pragma once

#include <Engine/Geometry/Mesh.hpp>

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Mesh/MeshAsset.hpp>

namespace Desert::Runtime
{
    class MeshService
    {
    public:
        // Eager: parse + build the GPU mesh now.
        Common::BoolResultStr Register( const std::shared_ptr<Assets::MeshAsset>& meshAsset );

        // Lazy: register the asset SHELL only — the .stmesh/.skmesh parse + GPU build are deferred to the
        // first Get. The shell must already carry its path-derived handle (set in the mesh-asset ctor).
        //
        // `resolveAgainst` IS REQUIRED, AND THAT IS THE POINT. Deferring the parse also defers everything
        // the parse tells the asset about its own dependencies, so the deferred load has to be able to
        // re-resolve them (AssetBase::EnsureLoaded). Passing the manager here rather than through a separate
        // setter means a caller cannot register a lazy shell and forget to say what it resolves against —
        // which is the mistake the compiler can catch and a comment cannot.
        Common::BoolResultStr RegisterAsset( const std::shared_ptr<Assets::MeshAsset>&  meshAsset,
                                             const std::weak_ptr<Assets::AssetManager>& resolveAgainst );

        Assets::AssetHandle RegisterProcedural( const std::shared_ptr<Mesh>& mesh );

        Mesh*              Get( const Assets::AssetHandle& handle ) const; // builds-on-miss from a shell
        Assets::MeshAsset* GetAsset( const Assets::AssetHandle& handle ) const;

        void                Clear();
        std::optional<bool> IsSkinned( const Assets::AssetHandle& handle ) const;

    private:
        // Load a shell that has not been parsed yet AND re-resolve what the parse just revealed. Both, or
        // neither: see AssetBase::EnsureLoaded.
        Common::BoolResultStr EnsureLoaded( const std::shared_ptr<Assets::MeshAsset>& meshAsset ) const;

        mutable std::unordered_map<Assets::AssetHandle, std::shared_ptr<Mesh>>      m_Meshes;
        std::unordered_map<Assets::AssetHandle, std::shared_ptr<Assets::MeshAsset>> m_MeshAssets;

        // The manager the deferred loads above resolve against. Weak, because the service is a
        // function-local static that outlives every project the editor opens, and a project switch must
        // leave a dead reference rather than a dangling one.
        std::weak_ptr<Assets::AssetManager> m_AssetManager;
    };
} // namespace Desert::Runtime