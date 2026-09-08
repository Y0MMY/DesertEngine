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

        // A prefab captured from a live entity has not been written yet — see CreateFromEntity below and
        // Unload's refusal.
        bool IsReloadableFromFile() const override
        {
            return !m_CapturedInMemory;
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
        // Set by CreateFromEntity and cleared by Load: this payload came from a live entity and no file
        // holds it yet, so releasing it destroys the only copy.
        bool m_CapturedInMemory = false;
    };
} // namespace Desert::Assets