#include "PrefabFactory.hpp"
#include <Engine/ECS/Components.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Core/Serialize/EntitySerializer.hpp>

namespace Desert::Runtime::Factory
{
    ECS::Entity PrefabFactory::Instantiate( const Assets::PrefabAsset& prefab, Core::Scene& scene,
                                            const Assets::AssetManager&       assetManager,
                                            std::unordered_set<Common::UUID>& stack )
    {
        if ( prefab.GetEntities().empty() )
            return {};

        const auto& prefabID = prefab.GetMetadata().Handle;

        if ( stack.contains( prefabID ) )
        {
            LOG_ERROR( "Prefab cyclic dependency detected for asset: {0}", prefab.GetMetadata().Filepath.string() );
            return {};
        }

        stack.insert( prefabID );

        std::unordered_map<Common::UUID, ECS::Entity> entityMap; // original prefab UUID → new entity
        ECS::Entity rootEntity = {};

        // 1. Create all entities with FRESH UUIDs to avoid collisions when multiple instances exist
        for ( const auto& data : prefab.GetEntities() )
        {
            Common::UUID originalID = data.id.value_or( Common::UUID{} );
            Common::UUID freshID;   // default-constructed generates a new random UUID
            ECS::Entity e = scene.CreateEntityWithUUID( freshID, data.Tag.value_or( "PrefabEntity" ) );
            entityMap[originalID] = e;

            if ( !rootEntity ) rootEntity = e;
        }

        // 2. Apply components and setup hierarchy
        for ( const auto& data : prefab.GetEntities() )
        {
            Common::UUID originalID = data.id.value_or( Common::UUID{} );
            ECS::Entity e = entityMap[originalID];

            if ( data.PrefabPath.has_value() )
            {
                auto nested = assetManager.FindByPath<Assets::PrefabAsset>( *data.PrefabPath );
                if ( nested )
                {
                    ECS::Entity nestedRoot = Instantiate( *nested, scene, assetManager, stack );
                    scene.Attach( e, nestedRoot );
                }
            }

            Core::Serialize::EntitySerializer::DeserializeEntity( data, e, assetManager );

            if ( data.parent.has_value() && *data.parent != Common::UUID{} )
            {
                // parent lookup uses original prefab UUIDs as keys
                if ( entityMap.contains( *data.parent ) )
                    scene.Attach( entityMap[*data.parent], e );
            }
        }

        // Ensure the root entity is tagged as a prefab instance
        if ( rootEntity && prefabID )
        {
            if ( !rootEntity.HasComponent<ECS::PrefabComponent>() )
                rootEntity.AddComponent<ECS::PrefabComponent>().Prefab = prefabID;
        }

        stack.erase( prefabID );
        return rootEntity;
    }

} // namespace Desert::Runtime::Factory