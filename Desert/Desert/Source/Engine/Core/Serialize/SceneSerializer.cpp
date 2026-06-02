#include <Engine/Core/Serialize/SceneSerializer.hpp>
#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Core/Serialize/EntitySerializer.hpp>
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

        for ( const auto& entity : m_Scene->GetAllEntities() )
        {
            scene.Entities.push_back( Serialize::EntitySerializer::SerializeEntity( entity, *m_AssetManager ) );
        }

        return rfl::json::write( scene );
    }

    void SceneSerializer::DeserializeFromJson( const std::string& json ) const
    {
        auto sceneData = rfl::json::read<SceneSerialized>( json );

        if ( !sceneData )
            return;

        m_Scene->SetSceneName( sceneData->SceneName );

        std::unordered_map<Common::UUID, ECS::Entity> entityMap;

        // 1. Create all entities
        for ( const auto& entityData : sceneData->Entities )
        {
            ECS::Entity entity = m_Scene->CreateEntityWithUUID( entityData.id, entityData.Tag.value_or( "Entity" ) );
            entityMap.insert({entityData.id, entity});
        }

        // 2. Restore components and hierarchy
        for ( const auto& entityData : sceneData->Entities )
        {
            auto it = entityMap.find(entityData.id);
            if (it == entityMap.end()) continue;

            ECS::Entity entity = it->second;
            Serialize::EntitySerializer::DeserializeEntity( entityData, entity, *m_AssetManager );

            if ( entityData.parent != Common::UUID{} )
            {
                auto parentIt = entityMap.find(entityData.parent);
                if ( parentIt != entityMap.end() )
                {
                    m_Scene->Attach( parentIt->second, entity );
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