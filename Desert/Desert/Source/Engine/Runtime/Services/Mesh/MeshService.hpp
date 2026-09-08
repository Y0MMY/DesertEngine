#pragma once

#include <Engine/Geometry/Mesh.hpp>

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Mesh/MeshAsset.hpp>

namespace Desert::Runtime
{
    class MeshService
    {
    public:
        // EAGER: PARSE (IF NEEDED) AND BUILD THE GPU MESH NOW — and the parenthesis is the fix, not a
        // clarification. This line has said "parse + build" since it was written and the body only ever
        // built: it called MeshFactory::Create on whatever the asset held at that instant. Every call site
        // handed it an asset it believed was loaded, and one of them was wrong in a way nothing could see —
        // `AssetManager::CreateAsset` DEDUPLICATES on a spelling-independent key, so a scene naming
        // `Cooked/Meshes/base.stmesh` got back AssetPreloader's UNPARSED shell for the absolute spelling of
        // the same file, this built a StaticMesh from 0 vertices / 0 submeshes, cached it under the handle,
        // and the `Load()` on the next line filled the ASSET while the cached MESH stayed empty forever.
        // Measured on the reproducer scene: file carries 1 submesh, the asset ends with 1, `Get` answered 0
        // ninety-one times in one 90-frame run and the frame was empty.
        //
        // So the load is inside now. Success means the built mesh matches the asset it was built from
        // (BuildAndCache asserts exactly that); failure names the file and the reason and caches NOTHING.
        NO_DISCARD Common::BoolResultStr Register( const std::shared_ptr<Assets::MeshAsset>& meshAsset );

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

        // THE ONLY PLACE A RUNTIME MESH IS BUILT FROM AN ASSET, so the relation below is asserted once
        // instead of in each of the two entry points — a check written into one of them is a check the
        // other silently skips, and that is precisely how the eager path shipped an empty mesh while the
        // lazy path was correct.
        //
        // The relation: A MESH BUILT FROM AN ASSET CARRIES THAT ASSET'S SUBMESHES. Both halves are
        // individually plausible — an asset with 1 submesh is a correct asset, a Mesh with 0 submeshes is a
        // constructible Mesh — so only their agreement catches the failure. On disagreement nothing is
        // cached and the caller is told, because a cached empty is permanent: `Get` answers from the cache
        // and never consults the asset again.
        Common::BoolResultStr BuildAndCache( const std::shared_ptr<Assets::MeshAsset>& meshAsset ) const;

        mutable std::unordered_map<Assets::AssetHandle, std::shared_ptr<Mesh>>      m_Meshes;
        std::unordered_map<Assets::AssetHandle, std::shared_ptr<Assets::MeshAsset>> m_MeshAssets;

        // The manager the deferred loads above resolve against. Weak, because the service is a
        // function-local static that outlives every project the editor opens, and a project switch must
        // leave a dead reference rather than a dangling one.
        std::weak_ptr<Assets::AssetManager> m_AssetManager;
    };
} // namespace Desert::Runtime