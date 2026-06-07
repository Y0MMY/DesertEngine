#include <Engine/Core/Serialize/SceneSerializer.hpp>
#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Core/Serialize/EntitySerializer.hpp>
#include <Engine/Runtime/Factory/PrefabFactory.hpp>
#include <Common/Utilities/FileSystem.hpp>
#include <Common/Core/Constants.hpp>
#include <rflcpp/rfl/json.hpp>
#include <regex>

namespace Desert::Core
{
    struct SceneSerialized
    {
        std::string                     SceneName;
        std::vector<Assets::EntityData> Entities;
    };

    SceneSerializer::SceneSerializer( const Scene* scene, const Assets::AssetManager* assetManager )
         : m_Scene( (Scene*)scene ), m_AssetManager( (Assets::AssetManager*)assetManager )
    {
    }

    std::string SceneSerializer::SerializeToJson() const
    {
        SceneSerialized scene;
        scene.SceneName = m_Scene->GetSceneName();

        // Helper to check if any ancestor has a PrefabComponent
        auto isPrefabChild = [&]( ECS::Entity entity ) -> bool
        {
            entt::entity current = entity.GetHandle();
            auto* registry = entity.GetRegistry();
            
            while ( registry->has<ECS::RelationshipComponent>( current ) )
            {
                const auto& rel = registry->get<ECS::RelationshipComponent>( current );
                if ( rel.Parent == entt::null ) break;
                
                current = rel.Parent;
                if ( registry->has<ECS::PrefabComponent>( current ) )
                {
                    return true;
                }
            }
            return false;
        };

        for ( const auto& entity : m_Scene->GetAllEntities() )
        {
            if ( isPrefabChild( const_cast<ECS::Entity&>(entity) ) )
            {
                continue;
            }
            scene.Entities.push_back( Serialize::EntitySerializer::SerializeEntity( entity, *m_AssetManager ) );
        }

        return rfl::json::write( scene );
    }

    void SceneSerializer::DeserializeFromJson( const std::string& json ) const
    {
        auto sceneData = rfl::json::read<SceneSerialized>( json );

        if ( !sceneData )
        {
            LOG_ERROR( "Failed to deserialize scene JSON: {0}", sceneData.error().what() );
            return;
        }

        LOG_INFO( "Loading scene: {0}", sceneData->SceneName );

        std::unordered_map<Common::UUID, ECS::Entity> entityMap;

        // 1. Create all entities
        for ( const auto& entityData : sceneData->Entities )
        {
            Common::UUID id = entityData.id.value_or( Common::UUID() );
            ECS::Entity entity = m_Scene->CreateEntityWithUUID( id, entityData.Tag.value_or( "Entity" ) );
            entityMap.insert({id, entity});
        }

        // 2. Restore components and hierarchy
        for ( const auto& entityData : sceneData->Entities )
        {
            Common::UUID id = entityData.id.value_or( Common::UUID{} );
            auto it = entityMap.find(id);
            if (it == entityMap.end()) continue;

            ECS::Entity entity = it->second;
            Serialize::EntitySerializer::DeserializeEntity( entityData, entity, *m_AssetManager );

            if ( entityData.parent.has_value() && *entityData.parent != Common::UUID{} )
            {
                auto parentIt = entityMap.find(*entityData.parent);
                if ( parentIt != entityMap.end() )
                {
                    m_Scene->Attach( parentIt->second, entity );
                }
            }

            if ( entityData.PrefabPath.has_value() )
            {
                 auto prefabAsset = m_AssetManager->FindByPath<Assets::PrefabAsset>( *entityData.PrefabPath );
                 if ( !prefabAsset )
                 {
                     prefabAsset = m_AssetManager->CreateAsset<Assets::PrefabAsset>( Assets::AssetPriority::High, *entityData.PrefabPath );
                 }

                 if ( prefabAsset )
                 {
                     std::unordered_set<Common::UUID> stack;
                     ECS::Entity prefabRoot = Runtime::Factory::PrefabFactory::Instantiate( *prefabAsset, *m_Scene, *m_AssetManager, stack );
                     if ( prefabRoot )
                     {
                         m_Scene->Attach( entity, prefabRoot );
                     }
                 }
            }
        }
    }

    void SceneSerializer::SaveToFile() const
    {
        const auto& serialized = SerializeToJson();
        auto        sceneName  = std::regex_replace( m_Scene->GetSceneName(), std::regex( "\\s+" ), "_" );
        sceneName += Common::Constants::Extensions::SCENE_EXTENSION;
        const Common::Filepath pathToSave = Common::Constants::Path::SCENE_PATH / sceneName;
        Common::Utils::FileSystem::WriteContentToFile( pathToSave, serialized );
    }

} // namespace Desert::Core