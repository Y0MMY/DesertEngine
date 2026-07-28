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

        Graphic::Material*    Get( const Assets::AssetHandle& handle ) const; // builds-on-miss from a shell
        Graphic::Material*    GetByExternalHandle( const Common::UUID& handle ) const;
        Assets::AssetHandle   GetAssetHandleByExternal( const Common::UUID& uuid ) const;
        void                  Clear();

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

    private:
        mutable std::unordered_map<Assets::AssetHandle, std::shared_ptr<Graphic::Material>> m_Materials;
        std::unordered_map<Common::UUID, Assets::AssetHandle>                               m_ExternalToInternal;
        std::unordered_map<Assets::AssetHandle, std::shared_ptr<Assets::MaterialAsset>>     m_MaterialAssets;

        // Invalidated materials awaiting safe destruction (see Invalidate/CollectGarbage).
        std::vector<std::shared_ptr<Graphic::Material>> m_Graveyard;
    };
} // namespace Desert::Runtime