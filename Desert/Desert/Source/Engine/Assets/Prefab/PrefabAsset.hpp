#pragma once

#include <Engine/Assets/AssetBase.hpp>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/AssetEvents.hpp>

#include "PrefabData.hpp"

namespace Desert::Assets
{
    class PrefabAsset : public AssetBase
    {
    public:
        explicit PrefabAsset( const AssetPriority priority, const Common::Filepath& filepath )
             : AssetBase( priority, filepath, AssetTypeID::Prefab )
        {
        }

        Common::BoolResultStr Load() override;
        Common::BoolResultStr Unload() override;

        bool IsReadyForUse() const override
        {
            return m_IsLoaded;
        }
        const std::vector<EntityData>& GetEntities() const
        {
            return m_EntityData;
        }

    private:
        std::vector<EntityData> m_EntityData;
        bool                    m_IsLoaded = false;
    };
} // namespace Desert::Assets