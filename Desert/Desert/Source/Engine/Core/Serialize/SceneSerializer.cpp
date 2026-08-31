#include <Engine/Core/Serialize/SceneSerializer.hpp>
#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Core/Serialize/EntitySerializer.hpp>
#include <Engine/Core/Serialize/SceneFormat.hpp>
#include <Engine/Core/Serialize/SceneStitchRules.hpp>
#include <Engine/Runtime/Factory/PrefabFactory.hpp>
#include <Engine/Reflection/ReflectionRegistry.hpp>
#include <Engine/Reflection/ReflectionSerializer.hpp>
#include <Engine/Core/SceneSettings.hpp>
#include <Engine/Graphic/Clouds/CloudTypeShape.hpp>
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

    Common::BoolResultStr SceneSerializer::DeserializeFromJson( const std::string& json,
                                                                std::string_view   source ) const
    {
        // THE VERSION GATE, AND WHY IT REFUSES INSTEAD OF REPAIRING.
        //
        // Eight schema migrations used to run right here, on every load of every scene, forever. Each was
        // written to be deleted "once no v<n> file remains" and not one ever was, because a migration that
        // runs at LOAD never writes its result back and so can never reach that condition - which is the
        // expiry DEV_CONTRACT §4.6 requires and the reason §4.3 ends "the runtime knows nothing about the
        // old format". This runtime now knows exactly one: the current one.
        //
        // It also does not SUBSTITUTE (§1.4). An old file is not loaded on defaults, not partially loaded
        // and not half-migrated: the gate is before the scene name, before the settings and before a single
        // entity is made, so a refusal creates nothing at all and the error says what to run. The
        // conversion still exists, in full, in Tools/SceneMigrator - it runs once, over the file, and
        // writes it back, which is the only shape of migration that can ever be finished.
        //
        // Callers ask ParseLoadableScene the same question BEFORE they clear the scene they are replacing;
        // this is the second, authoritative asking, so that a caller which forgets still cannot get an old
        // file past here.
        auto loadable = ParseLoadableScene( source, json );
        if ( !loadable )
        {
            LOG_ERROR( "{0}", loadable.GetError() );
            return Common::MakeError( loadable.GetError() );
        }

        const SceneSerialized scene = loadable.ExtractValue();

        LOG_INFO( "Loading scene: {0}", scene.SceneName );

        // Restore the scene name (was only logged before — so a renamed+saved scene reverted on load).
        if ( !scene.SceneName.empty() )
            m_Scene->SetSceneName( scene.SceneName );

        // Restore scene-wide settings (reflected). Missing keys keep their defaults (forward-compatible).
        if ( scene.Settings.has_value() )
        {
            if ( const auto* st = Reflection::ReflectionRegistry::Get().Find( "SceneSettings" ) )
                if ( auto obj = scene.Settings->to_object(); obj.has_value() )
                    Reflection::DeserializeReflected( *st, &m_Scene->GetSettings(), obj.value() );
        }

        // WHICH entity each record becomes, which one its payload lands on and what it hangs off is a pure
        // function of the parsed tree, and it lives in Rules::PlanSceneStitch so a test can call it: this
        // file cannot be compiled without the renderer, so for as long as the stitch was written out here
        // it was unreachable by every suite in the repository and a defect planted in it stayed green.
        // What remains below is the part only the loader can do — make the entities and feed the payloads.
        // InstantiatedLater: in a .desce a PrefabPath record names another FILE, and the entity it becomes
        // is made by pass 3 below out of that file - so it is listed here, not created.
        const Rules::StitchPlan plan = Rules::PlanSceneStitch( scene.Entities, &Common::UUID::Generate,
                                                               Rules::PrefabRecordPolicy::InstantiatedLater );

        // DC 1.4: a file that names one id twice, or names a parent that is not in it, loads as a scene
        // that is quietly missing pieces. Say which, once, instead of leaving it to be found in the viewport.
        if ( plan.Shadowed > 0 || plan.UnresolvedParents > 0 )
        {
            LOG_WARN( "[SceneSerializer] '{0}': {1} entity record(s) claim an id another record already "
                      "claimed (their payload is written onto the first claimant and their own entity stays "
                      "bare), and {2} parent link(s) name an entity this file does not contain. {3} id(s) "
                      "were minted for records that carried none.",
                      scene.SceneName, plan.Shadowed, plan.UnresolvedParents, plan.Minted );
        }

        std::unordered_map<Common::UUID, ECS::Entity> entityMap;

        // Pass 1 — create normal entities
        std::vector<ECS::Entity> created;
        created.reserve( plan.Created.size() );
        for ( const auto& plannedEntity : plan.Created )
        {
            const Assets::EntityData& entityData = scene.Entities[plannedEntity.Record];
            ECS::Entity               entity =
                 m_Scene->CreateEntityWithUUID( plannedEntity.Id, entityData.Tag.value_or( "Entity" ) );
            created.push_back( entity );
            entityMap.insert( { plannedEntity.Id, entity } );
        }

        // Pass 2 — deserialize normal entities and wire up hierarchy
        for ( const auto& load : plan.Loads )
        {
            ECS::Entity entity = created[load.Target];
            Serialize::EntitySerializer::DeserializeEntity( scene.Entities[load.Record], entity, *m_AssetManager );

            if ( load.Parent != Rules::kNoSlot )
                m_Scene->Attach( created[load.Parent], entity );
        }

        // Pass 3 — instantiate prefab roots and apply their saved transforms
        for ( const auto& plannedPrefab : plan.PrefabRecords )
        {
            const Assets::EntityData* entityData = &scene.Entities[plannedPrefab.Record];

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

            // Register in map under the original saved UUID so parent links resolve. A prefab root saved
            // without an id has nothing for a child's `parent` to name, so there is nothing to register:
            // the map entry would only shadow whatever else lacked an id.
            if ( entityData->id.has_value() && !entityData->id->IsNull() )
                entityMap[*entityData->id] = prefabRoot;

            // Attach to parent if one exists (e.g. prefab nested under a regular entity)
            if ( entityData->parent.has_value() && !entityData->parent->IsNull() )
            {
                auto parentIt = entityMap.find( *entityData->parent );
                if ( parentIt != entityMap.end() )
                    m_Scene->Attach( parentIt->second, prefabRoot );
            }
        }

        return BOOLSUCCESS;
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