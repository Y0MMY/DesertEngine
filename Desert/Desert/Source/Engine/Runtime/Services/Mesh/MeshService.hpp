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

        // DROP THE BUILT GPU MESH, KEEP THE SHELL. Returns true when something was actually dropped.
        //
        // The shell is what makes this safe to do at all: `Get()` builds on a miss from `m_MeshAssets`, so
        // the next draw rebuilds the vertex and index buffers through exactly the path a first use takes.
        // Forgetting the shell as well would turn the next `Get()` into a null — which is the silent empty
        // answer §1.4 forbids, and the reason this is not called `Release`.
        //
        // A PROCEDURAL MESH IS NOT DROPPED, whatever the caller asks. It was registered with no asset
        // behind it (`RegisterProcedural`), so there is nothing to rebuild it from and releasing it is
        // data loss rather than eviction. The ledger says the same thing in its own vocabulary by
        // claiming those buffers to `ResourceOwner::Procedural`; this is the enforcement.
        bool EvictBuilt( const Assets::AssetHandle& handle );

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