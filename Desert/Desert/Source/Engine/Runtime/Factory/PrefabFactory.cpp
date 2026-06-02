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

        std::unordered_map<Common::UUID, ECS::Entity> entityMap;
        ECS::Entity rootEntity = {};

        // 1. Create all entities first
        for ( const auto& data : prefab.GetEntities() )
        {
            ECS::Entity e = scene.CreateNewEntity( data.Tag.value_or( "PrefabEntity" ) );
            entityMap[data.id] = e;
            
            if ( !rootEntity ) rootEntity = e;
        }

        // 2. Apply components and setup hierarchy
        for ( const auto& data : prefab.GetEntities() )
        {
            ECS::Entity e = entityMap[data.id];

            if ( data.PrefabRef.has_value() )
            {
                auto nested = assetManager.FindByHandle<Assets::PrefabAsset>( *data.PrefabRef );
                if ( nested )
                {
                    ECS::Entity nestedRoot = Instantiate( *nested, scene, assetManager, stack );
                    scene.Attach( e, nestedRoot );
                }
            }

            Core::Serialize::EntitySerializer::DeserializeEntity( data, e, assetManager );

            if ( data.parent != Common::UUID{} )
            {
                if ( entityMap.contains( data.parent ) )
                {
                    scene.Attach( entityMap[data.parent], e );
                }
            }
        }

        stack.erase( prefabID );
        return rootEntity;
    }

} // namespace Desert::Runtime::Factory