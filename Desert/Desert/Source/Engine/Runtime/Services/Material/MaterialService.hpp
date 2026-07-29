#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Assets/MaterialAsset.hpp>

namespace Desert::Runtime
{
    class MaterialService
    {
    public:
        // Eager: build the runtime Material now.
        Common::BoolResultStr Register( const std::shared_ptr<Assets::MaterialAsset>& materialAsset );
        // Lazy: register the asset SHELL + the external->internal map only; the runtime Material (which binds
        // its textures) is built on the first Get.
        Common::BoolResultStr RegisterAsset( const std::shared_ptr<Assets::MaterialAsset>& materialAsset );

        // Builds-on-miss from a shell. A material-INSTANCE handle resolves through its parent
        // chain to the BASE material (an instance has no runtime Material of its own).
        Graphic::Material*    Get( const Assets::AssetHandle& handle ) const;
        Graphic::Material*    GetByExternalHandle( const Common::UUID& handle ) const;
        Assets::AssetHandle   GetAssetHandleByExternal( const Common::UUID& uuid ) const;
        void                  Clear();

        // THE way render systems obtain a slot's runtime instance. Base material asset -> a plain
        // instance of it; material-INSTANCE asset -> an instance of the parent chain's base
        // material with every level's overrides applied nearest-last (child wins). Returns null
        // when nothing resolves (caller falls back to its default material).
        // v1 note: instance assets override PARAMS only — texture overrides need per-instance
        // descriptors and are ignored by the batched path.
        Graphic::MaterialInstancePtr CreateRuntimeInstance( const Assets::AssetHandle& handle ) const;

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
        uint32_t m_InvalidationVersion = 0;
        mutable std::unordered_map<Assets::AssetHandle, std::shared_ptr<Graphic::Material>> m_Materials;
        std::unordered_map<Common::UUID, Assets::AssetHandle>                               m_ExternalToInternal;
        std::unordered_map<Assets::AssetHandle, std::shared_ptr<Assets::MaterialAsset>>     m_MaterialAssets;

        // Invalidated materials awaiting safe destruction (see Invalidate/CollectGarbage).
        std::vector<std::shared_ptr<Graphic::Material>> m_Graveyard;
    };
} // namespace Desert::Runtime