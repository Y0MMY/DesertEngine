#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Assets/MaterialAsset.hpp>

namespace Desert::Runtime
{
    class MaterialService
    {
    public:
        Common::BoolResultStr Register( const std::shared_ptr<Assets::MaterialAsset>& materialAsset );
        Graphic::Material*    Get( const Assets::AssetHandle& handle ) const;
        Graphic::Material*    GetByExternalHandle( const Common::UUID& handle ) const;
        Assets::AssetHandle   GetAssetHandleByExternal( const Common::UUID& uuid ) const;
        void                  Clear();

    private:
        std::unordered_map<Assets::AssetHandle, std::shared_ptr<Graphic::Material>> m_Materials;
        std::unordered_map<Common::UUID, Assets::AssetHandle>                       m_ExternalToInternal;
    };
} // namespace Desert::Runtime