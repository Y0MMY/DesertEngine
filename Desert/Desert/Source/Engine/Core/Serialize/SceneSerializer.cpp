#include <Engine/Core/Serialize/SceneSerializer.hpp>
#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Core/Serialize/EntitySerializer.hpp>
#include <Engine/Core/Serialize/SceneMigration.hpp>
#include <Engine/Runtime/Factory/PrefabFactory.hpp>
#include <Engine/Reflection/ReflectionRegistry.hpp>
#include <Engine/Reflection/ReflectionSerializer.hpp>
#include <Engine/Core/SceneSettings.hpp>
#include <Common/Utilities/FileSystem.hpp>
#include <Common/Core/Constants.hpp>
#include <Common/Core/Units.hpp>
#include <rflcpp/rfl/json.hpp>
#include <regex>

namespace Desert::Core
{
    SceneSerializer::SceneSerializer( const Scene* scene, const Assets::AssetManager* assetManager )
         : m_Scene( (Scene*)scene ), m_AssetManager( (Assets::AssetManager*)assetManager )
    {
    }

    std::string SceneSerializer::SerializeToJson() const
    {
        SceneSerialized scene;
        scene.SceneName    = m_Scene->GetSceneName();
        scene.UnitVersion  = kUnitVersion;
        scene.SceneVersion = kSceneVersion;

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

        // Scene-wide settings via the generic reflection serializer (no hand-written mirror struct).
        if ( const auto* st = Reflection::ReflectionRegistry::Get().Find( "SceneSettings" ) )
            scene.Settings = rfl::Generic( Reflection::SerializeReflected( *st, &m_Scene->GetSettings() ) );

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

        // Both migrations, HERE, on the parsed tree and before a single entity exists. Once the sky fields
        // left SkyboxComponent there is no component left for them to land in, and the load loop below
        // iterates the component REGISTRY rather than the file, so an old "Skybox" payload's sky values
        // would be read by nobody; the unit migration runs here for the plainer reason that a length is
        // easier to multiply once, in the file's own numbers, than to chase through a live scene graph.
        //
        // DC 4.7: never silently. Say which scene moved, from what, and by how much.
        const SceneMigrationReport migration = MigrateScene( *sceneData );
        if ( migration.SkyRaised )
        {
            LOG_INFO( "[SceneMigration] '{0}': sky schema v0 -> v{1} - {2} entity(ies), {3} carried, {4} "
                      "defaulted, {5} rejected",
                      sceneData->SceneName, kSceneVersion, migration.Sky.Entities, migration.Sky.FieldsCarried,
                      migration.Sky.FieldsDefaulted, migration.Sky.FieldsRejected );
        }
        if ( migration.UnitsRaised )
        {
            LOG_INFO( "[SceneMigration] '{0}': world units v0 -> v{1} (metres -> centimetres, x{2}) - {3} "
                      "entity(ies), {4} value(s) scaled, {5} rejected. Re-save (or run SceneMigrator) to "
                      "stamp the file so this never runs again.",
                      sceneData->SceneName, kUnitVersion, Common::Units::UnitsPerMetre, migration.Units.Entities,
                      migration.Units.Values, migration.Units.Rejected );
        }

        // Restore the scene name (was only logged before — so a renamed+saved scene reverted on load).
        if ( !sceneData->SceneName.empty() )
            m_Scene->SetSceneName( sceneData->SceneName );

        // Restore scene-wide settings (reflected). Missing keys keep their defaults (forward-compatible).
        if ( sceneData->Settings.has_value() )
        {
            if ( const auto* st = Reflection::ReflectionRegistry::Get().Find( "SceneSettings" ) )
                if ( auto obj = sceneData->Settings->to_object(); obj.has_value() )
                    Reflection::DeserializeReflected( *st, &m_Scene->GetSettings(), obj.value() );
        }

        // Split records: prefab-root entries are instantiated directly from their file;
        // normal entries follow the standard create-then-deserialize path.
        std::vector<const Assets::EntityData*> normalData;
        std::vector<const Assets::EntityData*> prefabData;

        for ( const auto& entityData : sceneData->Entities )
        {
            if ( entityData.PrefabPath.has_value() )
                prefabData.push_back( &entityData );
            else
                normalData.push_back( &entityData );
        }

        std::unordered_map<Common::UUID, ECS::Entity> entityMap;

        // Pass 1 — create normal entities
        for ( const auto* entityData : normalData )
        {
            Common::UUID id     = entityData->id.value_or( Common::UUID() );
            ECS::Entity  entity = m_Scene->CreateEntityWithUUID( id, entityData->Tag.value_or( "Entity" ) );
            entityMap.insert( { id, entity } );
        }

        // Pass 2 — deserialize normal entities and wire up hierarchy
        for ( const auto* entityData : normalData )
        {
            Common::UUID id = entityData->id.value_or( Common::UUID{} );
            auto         it = entityMap.find( id );
            if ( it == entityMap.end() ) continue;

            ECS::Entity entity = it->second;
            Serialize::EntitySerializer::DeserializeEntity( *entityData, entity, *m_AssetManager );

            if ( entityData->parent.has_value() && *entityData->parent != Common::UUID{} )
            {
                auto parentIt = entityMap.find( *entityData->parent );
                if ( parentIt != entityMap.end() )
                    m_Scene->Attach( parentIt->second, entity );
            }
        }

        // Pass 3 — instantiate prefab roots and apply their saved transforms
        for ( const auto* entityData : prefabData )
        {
            auto prefabAsset = m_AssetManager->FindByPath<Assets::PrefabAsset>( *entityData->PrefabPath );
            if ( !prefabAsset )
            {
                prefabAsset = m_AssetManager->CreateAsset<Assets::PrefabAsset>(
                    Assets::AssetPriority::High, *entityData->PrefabPath );
            }

            if ( !prefabAsset )
            {
                LOG_ERROR( "SceneSerializer: could not load prefab '{0}'", *entityData->PrefabPath );
                continue;
            }

            if ( !prefabAsset->IsReadyForUse() )
                prefabAsset->Load();

            std::unordered_set<Common::UUID> stack;
            ECS::Entity prefabRoot = Runtime::Factory::PrefabFactory::Instantiate(
                *prefabAsset, *m_Scene, *m_AssetManager, stack );

            if ( !prefabRoot )
                continue;

            // Apply the transform that was saved in the scene for this prefab root
            if ( entityData->Translation || entityData->Rotation || entityData->Scale )
            {
                auto& tc = prefabRoot.HasComponent<ECS::TransformComponent>()
                    ? prefabRoot.GetComponent<ECS::TransformComponent>()
                    : prefabRoot.AddComponent<ECS::TransformComponent>();
                if ( entityData->Translation ) tc.Translation = *entityData->Translation;
                if ( entityData->Rotation )    tc.Rotation    = *entityData->Rotation;
                if ( entityData->Scale )       tc.Scale       = *entityData->Scale;
            }

            // Register in map under the original saved UUID so parent links resolve
            Common::UUID savedID = entityData->id.value_or( Common::UUID{} );
            entityMap[savedID]   = prefabRoot;

            // Attach to parent if one exists (e.g. prefab nested under a regular entity)
            if ( entityData->parent.has_value() && *entityData->parent != Common::UUID{} )
            {
                auto parentIt = entityMap.find( *entityData->parent );
                if ( parentIt != entityMap.end() )
                    m_Scene->Attach( parentIt->second, prefabRoot );
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