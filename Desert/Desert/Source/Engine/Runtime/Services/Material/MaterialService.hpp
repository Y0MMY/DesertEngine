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

    private:
        mutable std::unordered_map<Assets::AssetHandle, std::shared_ptr<Graphic::Material>> m_Materials;
        std::unordered_map<Common::UUID, Assets::AssetHandle>                               m_ExternalToInternal;
        std::unordered_map<Assets::AssetHandle, std::shared_ptr<Assets::MaterialAsset>>     m_MaterialAssets;
    };
} // namespace Desert::Runtime