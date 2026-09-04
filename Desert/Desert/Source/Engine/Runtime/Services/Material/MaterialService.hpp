#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Materials/MaterialOverrides.hpp>
#include <Engine/Graphic/Materials/Mesh/MeshVertexPath.hpp>
#include <Engine/Assets/MaterialAsset.hpp>
#include <Engine/Runtime/Services/Material/MaterialIdentity.hpp>

#include <array>

namespace Desert::Runtime
{
    // Owns the runtime materials behind the `.demat` assets.
    //
    // A runtime material is identified by the PAIR (asset, vertex path), not by the asset alone. That is
    // the difference between "an artist authors a surface" and "the renderer decides how the geometry is
    // fetched": one `.demat` legitimately becomes a static material AND a skinned material AND an
    // instanced one, all with the same parameters, because they are built from the same asset. Resolving
    // an asset into ONE material — as this service used to — makes the material's class the vertex path,
    // and a mesh on any other path then has nothing it can be drawn with. See
    // Engine/Graphic/Materials/Mesh/MeshVertexPath.hpp for the four defects that followed from it.
    class MaterialService
    {
    public:
        // Eager: build the runtime Material now.
        //
        // REFUSES, naming both files, when the handle is already held by a DIFFERENT `.demat` (DC 1.4):
        // the first registration keeps the identity and the second is rejected rather than silently
        // taking it over. Before this, whichever material registered second won the service map and the
        // other could never resolve — with nothing logged to say a material had been displaced.
        Common::BoolResultStr Register( const std::shared_ptr<Assets::MaterialAsset>& materialAsset );
        // Lazy: register the asset SHELL + the external->internal map only; the runtime Material (which binds
        // its textures) is built on the first Get. Refuses a colliding identity on the same terms as
        // Register above.
        Common::BoolResultStr RegisterAsset( const std::shared_ptr<Assets::MaterialAsset>& materialAsset );

        // Builds-on-miss from a shell, for ONE vertex path. A material-INSTANCE handle resolves through
        // its parent chain to the BASE material (an instance has no runtime Material of its own).
        // Returns null when the asset resolves to nothing, or when its shader has no variant on that
        // path (a custom DSL surface shader on the skinned path, say) — MaterialFactory names which.
        //
        // The default is Static because most callers ask "does this asset resolve to a material at all?"
        // and any path answers that; the mesh renderers pass the path they are about to draw with.
        Graphic::Material*  Get( const Assets::AssetHandle& handle,
                                 Graphic::MeshVertexPath    path = Graphic::MeshVertexPath::Static ) const;
        Graphic::Material*  GetByExternalHandle( const Common::UUID& handle ) const;
        Assets::AssetHandle GetAssetHandleByExternal( const Common::UUID& uuid ) const;
        void                Clear();

        // Every runtime material ALREADY BUILT for this handle, one per path that has been asked for.
        // Live editing has to reach all of them: a parameter edit that updated only the static material
        // would leave the character wearing the old value, and that divergence is precisely what per-path
        // material classes used to guarantee. Never builds; a path nobody has asked for is not returned.
        std::vector<Graphic::Material*> GetBuiltVariants( const Assets::AssetHandle& handle ) const;

        // THE way render systems obtain a slot's runtime instance. Base material asset -> a plain
        // instance of it; material-INSTANCE asset -> an instance of the parent chain's base
        // material with every level's overrides applied nearest-last (child wins). Returns null
        // when nothing resolves (caller falls back to its default material).
        // v1 note: instance assets override PARAMS only — texture overrides need per-instance
        // descriptors and are ignored by the batched path.
        //
        // The PATH is the caller's, because the caller is the one that knows what geometry it is about to
        // draw. A skinned mesh component asks for Skinned and gets an instance of the same `.demat` the
        // static twin beside it uses; before the path existed, it asked for "the" material, got the static
        // one, and the renderer dropped it.
        Graphic::MaterialInstancePtr
        CreateRuntimeInstance( const Assets::AssetHandle& handle,
                               Graphic::MeshVertexPath    path = Graphic::MeshVertexPath::Static ) const;

        // The same resolution as CreateRuntimeInstance, but delivered as NAMED VALUES instead of a runtime
        // instance — for a renderer that owns one material of its own and applies a material's parameters
        // to it by name. The terrain is that renderer and is currently the only one: its surface is a
        // single Terrain-domain program whose three splat layers are texture parameters of that program,
        // so a terrain material has no per-object Graphic::MaterialInstance to be.
        //
        // Appends base-first, child-last, so a material INSTANCE's override wins where both name a
        // parameter — the same "nearest wins" order CreateRuntimeInstance applies, and the order the
        // consumers of MaterialOverrides already assume (last write wins).
        //
        // Appends rather than returns so the caller can hand the render command the vectors it already
        // owns; this runs once per terrain entity per frame. Returns false when the handle resolves to no
        // material at all, which is what an unset slot means: the caller then leaves the shader's own
        // schema defaults in place.
        bool ResolveOverrides( const Assets::AssetHandle& handle, Graphic::MaterialOverrides& out ) const;

        // For editor live-edit of a material-instance asset: entities rebuild their cached
        // runtime instances on the next tick (same mechanism as Invalidate, no graveyard needed —
        // no runtime Material dies here).
        void BumpInvalidationVersion()
        {
            ++m_InvalidationVersion;
        }

        // Drops the built runtime Material so the next Get() rebuilds it from the asset shell.
        // Needed when the asset's SHADER changes (a different runtime material class entirely);
        // plain parameter edits use the Apply*Asset fast path instead.
        //
        // The old material is NOT destroyed here: its descriptor pools may still be referenced
        // by the command buffer being recorded / frames in flight (destroying them mid-frame
        // invalidates the command buffer -> device lost). It parks in a graveyard until
        // CollectGarbage() runs at a safe point.
        void Invalidate( const Assets::AssetHandle& handle );

        // Destroys invalidated materials. Call at the START of a frame (before any command
        // recording); waits for the device to go idle first, so no in-flight frame can still
        // reference the dying descriptor pools. No-op (and free) when the graveyard is empty.
        void CollectGarbage();

        // Monotonic stamp, bumped on every Invalidate(). Consumers that cache raw Material* /
        // instances (the mesh components' RuntimeMaterialInstances) compare their stored stamp
        // and rebuild when it moved — dangling parent pointers become impossible without any
        // "remember to clear the instances" discipline at the Invalidate call sites.
        uint32_t GetInvalidationVersion() const
        {
            return m_InvalidationVersion;
        }

    private:
        // BOOLSUCCESS when `handle` is free, or held by the very file that is registering again. Otherwise
        // it LOGS both filepaths and returns the same text as an error, and the caller writes nothing.
        Common::BoolResultStr RefuseOnCollision( const Assets::AssetHandle&                    handle,
                                                 const std::shared_ptr<Assets::MaterialAsset>& incoming ) const;

        // One slot per vertex path, built lazily. An array and not a second map because the paths are a
        // closed set the renderer enumerates — a map would let a path exist that no draw can ask for.
        using PathVariants = std::array<std::shared_ptr<Graphic::Material>, Graphic::kMeshVertexPathCount>;

        uint32_t                                                                        m_InvalidationVersion = 0;
        mutable std::unordered_map<Assets::AssetHandle, PathVariants>                   m_Materials;
        std::unordered_map<Common::UUID, Assets::AssetHandle>                           m_ExternalToInternal;
        std::unordered_map<Assets::AssetHandle, std::shared_ptr<Assets::MaterialAsset>> m_MaterialAssets;

        // Invalidated materials awaiting safe destruction (see Invalidate/CollectGarbage).
        std::vector<std::shared_ptr<Graphic::Material>> m_Graveyard;
    };
} // namespace Desert::Runtime