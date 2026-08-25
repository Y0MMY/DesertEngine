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

        // The entity each prefab record became, aligned with GetEntities().
        //
        // Pass 2 used to re-derive the map key from the record and index `entityMap` with it. For a record
        // with no `id` that key was a fresh value each time, so the lookup missed AND `operator[]` inserted
        // a null entity, which the deserializer then wrote components into. Holding the entity directly
        // removes both the second derivation and the accidental insert; `entityMap` is left to do the one
        // job it is actually for, resolving `parent` links between records that DO carry ids.
        std::vector<ECS::Entity> created;
        created.reserve( prefab.GetEntities().size() );

        // 1. Create all entities with FRESH UUIDs to avoid collisions when multiple instances exist
        for ( const auto& data : prefab.GetEntities() )
        {
            ECS::Entity e =
                 scene.CreateEntityWithUUID( Common::UUID::Generate(), data.Tag.value_or( "PrefabEntity" ) );
            created.push_back( e );

            if ( data.id.has_value() && !data.id->IsNull() )
                entityMap[*data.id] = e;

            if ( !rootEntity ) rootEntity = e;
        }

        // 2. Apply components and setup hierarchy
        for ( size_t i = 0; i < prefab.GetEntities().size(); ++i )
        {
            const auto& data = prefab.GetEntities()[i];
            ECS::Entity e    = created[i];

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

            if ( data.parent.has_value() && !data.parent->IsNull() )
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