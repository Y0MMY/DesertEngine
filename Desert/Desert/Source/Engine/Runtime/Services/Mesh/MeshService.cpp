#include "MeshService.hpp"

#include <Engine/Geometry/MeshFactory.hpp>

namespace Desert::Runtime
{
    namespace
    {
        // The two device buffers a mesh IS, named as the asset's. A procedural mesh is claimed differently
        // in RegisterProcedural: it has no file, so nothing may ever release it. See ResourceLedger.hpp.
        void ClaimMeshBuffers( const std::shared_ptr<Mesh>& mesh, const Graphic::ResourceOwner owner,
                               const Assets::AssetHandle& asset )
        {
            if ( !mesh )
                return;
            if ( const auto& vertices = mesh->GetVertexBuffer() )
            {
                vertices->ClaimOwnership( owner, asset );
                // The device bytes, from the buffer itself. This is the one place a mesh's two buffers are
                // both in hand with the asset they came from — see Engine/Graphic/ResourceLedger.hpp.
                vertices->RecordDeviceBytes( vertices->GetSize() );
            }
            if ( const auto& indices = mesh->GetIndexBuffer() )
            {
                indices->ClaimOwnership( owner, asset );
                indices->RecordDeviceBytes( indices->GetSize() );
            }
        }
    } // namespace

    Common::BoolResultStr MeshService::Register( const std::shared_ptr<Assets::MeshAsset>& meshAsset )
    {
        if ( !meshAsset )
        {
            return Common::MakeError( "Mesh asset is null" );
        }
        if ( !meshAsset->GetMetadata().IsValid() )
        {
            return Common::MakeError( "Mesh asset is invalid" );
        }

        // THE SHELL BEFORE THE BUILD. Recorded first so that a build which fails (a skinned mesh whose rig
        // is not in the manager yet, a cooked file that will not parse) still leaves something `Get` can
        // retry from — and so BuildAndCache's own EnsureLoaded has the asset on record while it runs.
        m_MeshAssets[meshAsset->GetMetadata().Handle] = meshAsset;

        return BuildAndCache( meshAsset );
    }

    Common::BoolResultStr MeshService::RegisterAsset( const std::shared_ptr<Assets::MeshAsset>&  meshAsset,
                                                      const std::weak_ptr<Assets::AssetManager>& resolveAgainst )
    {
        if ( !meshAsset )
            return Common::MakeError( "Mesh asset is null" );
        if ( resolveAgainst.expired() )
            return Common::MakeError( "Mesh asset '" + meshAsset->GetMetadata().Filepath.string() +
                                      "' was registered as a lazy shell against an AssetManager that is "
                                      "already gone; its deferred load could never resolve dependencies." );

        // Handle is path-derived in the ctor, so a not-yet-loaded shell is keyed correctly. The .stmesh parse
        // + GPU build are deferred to the first Get/GetAsset.
        m_MeshAssets[meshAsset->GetMetadata().Handle] = meshAsset;
        m_AssetManager                                = resolveAgainst;
        return BOOLSUCCESS;
    }

    Common::BoolResultStr MeshService::EnsureLoaded( const std::shared_ptr<Assets::MeshAsset>& meshAsset ) const
    {
        if ( meshAsset->IsReadyForUse() )
            return BOOLSUCCESS;

        const auto manager = m_AssetManager.lock();
        if ( !manager )
        {
            // Loudly, and then not at all: a skinned mesh parsed without a manager reports a skeleton
            // signature it cannot look up, MeshFactory refuses to build it, and the frame contains nothing
            // with no other trace anywhere. Naming the file and the reason is the whole difference between
            // this and the defect it replaces.
            return Common::MakeError( "MeshService: '" + meshAsset->GetMetadata().Filepath.string() +
                                      "' needs a deferred load but no AssetManager is bound — the shell was "
                                      "never registered through RegisterAsset, or its project has closed." );
        }

        return meshAsset->EnsureLoaded( *manager );
    }

    Common::BoolResultStr MeshService::BuildAndCache( const std::shared_ptr<Assets::MeshAsset>& meshAsset ) const
    {
        // The payload first. Building from an unparsed shell produces a Mesh with no geometry in it, and
        // that Mesh is then the cached answer for the life of the process — see the header on Register.
        if ( const auto loaded = EnsureLoaded( meshAsset ); !loaded )
            return loaded;

        const std::string path   = meshAsset->GetMetadata().Filepath.string();
        const auto        handle = meshAsset->GetMetadata().Handle;

        auto mesh = Graphic::MeshFactory::Create( meshAsset );
        if ( !mesh )
        {
            // MeshFactory has already said which of its preconditions failed; this adds the file, which it
            // does not have. Nothing is cached: the comment this replaces was right that a sticky null can
            // never recover once the dependency is in place.
            return Common::MakeFormattedError( "MeshService: no runtime mesh could be built for '{}'", path );
        }

        // THE RELATION. Asserted rather than assumed, because both sides are individually well-formed and
        // only their disagreement is the defect.
        if ( mesh->GetSubmeshes().size() != meshAsset->GetSubmeshes().size() )
        {
            return Common::MakeFormattedError(
                 "MeshService: '{}' holds {} submesh(es) but the runtime mesh built from it has {} — it "
                 "would draw nothing while looking like a built mesh, so it is NOT cached.",
                 path, meshAsset->GetSubmeshes().size(), mesh->GetSubmeshes().size() );
        }

        m_Meshes[handle] = std::move( mesh );
        ClaimMeshBuffers( m_Meshes[handle], Graphic::ResourceOwner::AssetService, handle );
        return BOOLSUCCESS;
    }

    Assets::AssetHandle MeshService::RegisterProcedural( const std::shared_ptr<Mesh>& mesh )
    {
        // Procedural meshes have no source path, so mint a fresh random id (the default handle is now Null).
        Assets::AssetHandle handle = Assets::AssetHandle::Generate();
        // Build the GPU vertex/index buffers — the mesh ctor doesn't (asset meshes get this via
        // MeshFactory::Create, and the ECS primitive path Invalidates explicitly). Without this a builtin
        // procedural mesh (e.g. the Cube) has no buffers and renders nothing.
        if ( mesh )
        {
            // Reported, not refused: the caller receives a handle either way and the registry is the
            // only place this mesh can be found again. What must not happen is the previous behaviour —
            // a mesh whose buffers never uploaded sitting in the registry, drawing nothing, with the
            // handle looking exactly like a working one.
            const auto uploaded = mesh->Invalidate();
            if ( !uploaded.IsSuccess() )
                LOG_ERROR( "[MeshService] procedural mesh {} has no GPU buffers: {}", (uint64_t)handle,
                           uploaded.GetError() );
        }
        // `Procedural`, NOT `AssetService`, and the distinction is load-bearing rather than cosmetic: this
        // mesh was built from no file, so there is no recipe to rebuild it from and releasing it is data
        // loss. The ledger's owner category is what asset eviction reads to know it must not touch this.
        ClaimMeshBuffers( mesh, Graphic::ResourceOwner::Procedural, handle );
        m_Meshes[handle] = mesh;
        return handle;
    }

    Desert::Mesh* MeshService::Get( const Assets::AssetHandle& handle ) const
    {
        if ( auto it = m_Meshes.find( handle ); it != m_Meshes.end() )
            return it->second.get();

        // Lazy build: a shell was registered — parse the .stmesh (if needed) + build the GPU mesh now.
        // Through the same BuildAndCache the eager path uses, so both routes obey the same relation and a
        // FAILED build caches nothing: a sticky null (or a sticky empty) can never recover once the
        // dependency it was missing is in place.
        if ( auto ait = m_MeshAssets.find( handle ); ait != m_MeshAssets.end() )
        {
            if ( const auto built = BuildAndCache( ait->second ); !built )
            {
                LOG_ERROR( "MeshService::Get: {}", built.GetError() );
                return nullptr;
            }
            return m_Meshes[handle].get();
        }
        return nullptr;
    }

    Assets::MeshAsset* MeshService::GetAsset( const Assets::AssetHandle& handle ) const
    {
        auto it = m_MeshAssets.find( handle );
        if ( it == m_MeshAssets.end() )
            return nullptr;
        // Ensure the payload is parsed before callers read submeshes / material handles (lazy shells).
        if ( const auto loaded = EnsureLoaded( it->second ); !loaded )
        {
            LOG_ERROR( "MeshService::GetAsset: {}", loaded.GetError() );
            return nullptr;
        }
        return it->second.get();
    }

    bool MeshService::EvictBuilt( const Assets::AssetHandle& handle )
    {
        const auto built = m_Meshes.find( handle );
        if ( built == m_Meshes.end() )
            return false;

        // No shell means no recipe: this is a procedural mesh registered by RegisterProcedural, and the
        // only copy of its geometry is the buffers about to be dropped. Refuse, and say so — a silent
        // "nothing to do" here would read to the caller as "already released".
        if ( m_MeshAssets.find( handle ) == m_MeshAssets.end() )
        {
            LOG_WARN( "[MeshService] eviction asked for mesh {} and it is procedural — no asset shell, so "
                      "nothing could rebuild it. Kept.",
                      static_cast<uint64_t>( handle ) );
            return false;
        }

        m_Meshes.erase( built );
        return true;
    }

    void MeshService::Clear()
    {
        m_Meshes.clear();
        m_MeshAssets.clear();
        m_AssetManager.reset();
    }

    std::optional<bool> MeshService::IsSkinned( const Assets::AssetHandle& handle ) const
    {
        // Cheap query: the concrete asset subclass (Static vs Skinned, chosen by extension at registration)
        // encodes skinned-ness via a virtual constant — no need to parse the .stmesh or build the GPU mesh.
        // This must stay cheap: UI (e.g. the mesh selector popup) calls it for EVERY mesh asset every frame.
        // Building here would force a synchronous load+GPU-build of every mesh in the project → multi-second
        // freeze that looks like a hang.
        if ( auto ait = m_MeshAssets.find( handle ); ait != m_MeshAssets.end() )
            return std::make_optional( ait->second->IsSkinned() );

        // Procedural meshes have no asset shell — fall back to the already-built runtime mesh if present.
        if ( auto it = m_Meshes.find( handle ); it != m_Meshes.end() )
            return std::make_optional( it->second->IsSkinned() );

        return std::nullopt;
    }

} // namespace Desert::Runtime
