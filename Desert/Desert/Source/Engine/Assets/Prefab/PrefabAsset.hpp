#pragma once

#include <Engine/Assets/AssetBase.hpp>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/AssetEvents.hpp>
#include <Engine/ECS/Entity.hpp>

#include "PrefabData.hpp"

namespace Desert::Core
{
    class Scene;
}

namespace Desert::Assets
{
    class PrefabAsset : public AssetBase
    {
    public:
        static AssetTypeID GetTypeID()
        {
            return AssetTypeID::Prefab;
        }

        explicit PrefabAsset( const AssetPriority priority, const Common::Filepath& filepath )
             : AssetBase( priority, filepath, AssetTypeID::Prefab )
        {
        }

        Common::BoolResultStr Load() override;
        Common::BoolResultStr Unload() override;

        std::string Serialize() const;

        bool IsReadyForUse() const override
        {
            return m_IsLoaded;
        }
        const std::vector<EntityData>& GetEntities() const
        {
            return m_EntityData;
        }

        void CreateFromEntity( ECS::Entity rootEntity, const AssetManager& assetManager );
        
        ECS::Entity Instantiate( Core::Scene* scene, const AssetManager& assetManager, const glm::vec3* position = nullptr ) const;

    private:
        std::vector<EntityData> m_EntityData;
        bool                    m_IsLoaded = false;
    };
} // namespace Desert::Assets