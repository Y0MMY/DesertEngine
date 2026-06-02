#pragma once

#include <Engine/ECS/Entity.hpp>
#include <Engine/Assets/Prefab/PrefabData.hpp>
#include <Engine/Assets/AssetManager.hpp>

namespace Desert::Core::Serialize
{
    class EntitySerializer
    {
    public:
        static Assets::EntityData SerializeEntity( ECS::Entity entity, const Assets::AssetManager& assetManager );
        static void DeserializeEntity( const Assets::EntityData& data, ECS::Entity entity, const Assets::AssetManager& assetManager );
    };
} // namespace Desert::Core::Serialize