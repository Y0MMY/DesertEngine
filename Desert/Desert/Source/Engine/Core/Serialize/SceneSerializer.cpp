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
    // World-unit generation of the scene file. Absent (or 0) means the scene was authored when one world
    // unit was one METRE; today a unit is a CENTIMETRE (Common/Core/Units.hpp), so such a scene is scaled
    // ×100 on load — see MigrateMetresToUnits(). Bump this only if the world unit changes again.
    static constexpr int kUnitVersion = 1;

    struct SceneSerialized
    {
        std::string                     SceneName;
        std::vector<Assets::EntityData> Entities;
        // Scene-wide settings — reflected, so the whole block round-trips through the generic serializer.
        std::optional<rfl::Generic>     Settings;
        std::optional<int>              UnitVersion;
        // Schema generation, absent => 0. A SECOND version integer on purpose - see kSceneVersion in
        // SceneMigration.hpp for why it must not be folded into UnitVersion.
        std::optional<int> SceneVersion;
    };

    namespace
    {
        // One-shot upgrade of a metres-era scene to centimetre world units. Every distance an entity owns
        // is scaled: positions always, and Scale only for FILE-backed meshes — a procedural primitive's
        // geometry is regenerated at the new size by the factory, so scaling it too would cube the object.
        void MigrateMetresToUnits( Core::Scene& scene )
        {
            constexpr float S = Common::Units::UnitsPerMetre;

            for ( const auto& e : scene.GetAllEntities() )
            {
                ECS::Entity entity = e;
                if ( entity.HasComponent<ECS::TransformComponent>() )
                {
                    auto& tc = entity.GetComponent<ECS::TransformComponent>();
                    tc.Translation *= S;

                    const bool proceduralMesh =
                         entity.HasComponent<ECS::StaticMeshComponent>() &&
                         entity.GetComponent<ECS::StaticMeshComponent>().Primitive.has_value();
                    if ( !proceduralMesh )
                        tc.Scale *= S;
                }
                if ( entity.HasComponent<ECS::CameraComponent>() )
                {
                    auto& d = entity.GetComponent<ECS::CameraComponent>().Data;
                    d.Near *= S, d.Far *= S;
                }
                if ( entity.HasComponent<ECS::PointLightComponent>() )
                {
                    auto& d = entity.GetComponent<ECS::PointLightComponent>().Data;
                    d.Radius *= S, d.MinRadius *= S;
                }
                if ( entity.HasComponent<ECS::SpotLightComponent>() )
                    entity.GetComponent<ECS::SpotLightComponent>().Data.Range *= S;
                if ( entity.HasComponent<ECS::ColliderComponent>() )
                {
                    auto& d = entity.GetComponent<ECS::ColliderComponent>().Data;
                    d.HalfExtents *= S;
                    d.Radius *= S, d.HalfHeight *= S;
                }
                if ( entity.HasComponent<ECS::CharacterControllerComponent>() )
                {
                    auto& d = entity.GetComponent<ECS::CharacterControllerComponent>().Data;
                    d.Radius *= S, d.Height *= S, d.Gravity *= S;
                }
                if ( entity.HasComponent<ECS::TerrainComponent>() )
                {
                    auto& d = entity.GetComponent<ECS::TerrainComponent>().Data;
                    d.Size *= S, d.HeightScale *= S, d.GrassHeight *= S;
                }
                if ( entity.HasComponent<ECS::TextComponent>() )
                    entity.GetComponent<ECS::TextComponent>().Size *= S;
            }

            scene.GetSettings().Gravity *= S;
            LOG_INFO( "SceneSerializer: migrated a metres-era scene to centimetre world units (x{0})", S );
        }
    } // namespace

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

        // Sky schema migration - HERE, on the parsed tree, and not after the entities exist. Once the sky
        // fields left SkyboxComponent there is no component left for them to land in, and the load loop
        // below iterates the component REGISTRY rather than the file, so an old "Skybox" payload's sky
        // values would be read by nobody and the scene would open with a default sky.
        //
        // Independent of MigrateMetresToUnits at the bottom of this function, and it has to be: an old
        // metres-era scene runs BOTH in one load. They cannot interact because no sky field is a length -
        // colours, multipliers, angles and a radius already authored in kilometres.
        if ( sceneData->SceneVersion.value_or( 0 ) < kSceneVersion )
        {
            const SkyMigrationReport report = MigrateSkyV0ToV1( sceneData->Entities );
            LOG_INFO( "[SceneMigration] '{0}': sky schema v0 -> v1 - {1} entity(ies), {2} carried, {3} "
                      "defaulted, {4} rejected",
                      sceneData->SceneName, report.Entities, report.FieldsCarried, report.FieldsDefaulted,
                      report.FieldsRejected );
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

        // Scenes written before the world unit became a centimetre carry no UnitVersion — upgrade them
        // once here, so opening an old level still shows it at the right size (re-saving stamps v1).
        // Runs on the LIVE scene, unlike the sky migration above, and neither depends on the other.
        if ( sceneData->UnitVersion.value_or( 0 ) < kUnitVersion )
            MigrateMetresToUnits( *m_Scene );
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