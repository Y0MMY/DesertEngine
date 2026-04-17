#include "PrefabFactory.hpp"

namespace Desert::Runtime::Factory
{
    void CopyComponents( const Assets::EntityData& data, ECS::Entity entity )
    {
        if ( data.Tag )
        {
            entity.AddComponent<ECS::TagComponent>().Tag = data.Tag.value();
        }

       /* if ( data.Transform )
            entity.GetComponent<TransformComponent>() = *data.Transform;*/
    }

    ECS::Entity PrefabFactory::Instantiate( const Assets::PrefabAsset& prefab, Core::Scene& scene,
                                            const Assets::AssetManager&       assetManager,
                                            std::unordered_set<Common::UUID>& stack )
    {
        std::unordered_map<Common::UUID, ECS::Entity> entityMap;

     /*   const auto& prefabID = prefab.GetMetadata().Handle;

        if ( stack.contains( prefabID ) )
        {
            throw std::runtime_error( "Prefab cyclic dependency detected" );
        }

        stack.insert( prefabID );

        for ( const auto& data : prefab.GetEntities() )
        {
            ECS::Entity e      = scene.CreateNewEntity( "PrefabEntity" );
            entityMap[data.id] = e;
        }

        for ( const auto& data : prefab.GetEntities() )
        {
            ECS::Entity e = entityMap[data.id];

            if ( data.PrefabRef.has_value() )
            {
                auto nested = assetManager.FindByHandle<Assets::PrefabAsset>( *data.PrefabRef );

                ECS::Entity childRoot = Instantiate( *nested, scene, assetManager, stack );

                scene.Attach( e, childRoot );
                continue;
            }

            CopyComponents( data, e );
        }

        for ( const auto& data : prefab.GetEntities() )
        {
            if ( data.parent != Common::UUID{} )
            {
                scene.Attach( entityMap[data.parent], entityMap[data.id] );
            }
        }

        stack.erase( prefabID );*/

        return entityMap.at( prefab.GetEntities().front().id );
    }

} // namespace Desert::Runtime::Factory