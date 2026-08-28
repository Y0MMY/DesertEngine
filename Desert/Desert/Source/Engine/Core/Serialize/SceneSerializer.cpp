#include <Engine/Core/Serialize/SceneSerializer.hpp>
#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Core/Serialize/EntitySerializer.hpp>
#include <Engine/Core/Serialize/SceneMigration.hpp>
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
                      sceneData->SceneName, kSceneVersionSky, migration.Sky.Entities, migration.Sky.FieldsCarried,
                      migration.Sky.FieldsDefaulted, migration.Sky.FieldsRejected );
        }
        if ( migration.TonemapperRaised && migration.Tonemap.OperatorPinned )
        {
            LOG_INFO( "[SceneMigration] '{0}': scene schema v{1} -> v{2} - the tonemapper is now a scene "
                      "property and this file predates it, so it was pinned to the operator it was "
                      "authored on (Reinhard){3}. Re-save (or run SceneMigrator) to stamp the file so "
                      "this never runs again.",
                      sceneData->SceneName, kSceneVersionSky, kSceneVersionTonemap,
                      migration.Tonemap.SettingsCreated ? ", in a settings block created for it" : "" );
        }
        if ( migration.CloudNoiseRaised && migration.CloudNoise.Entities > 0 )
        {
            LOG_INFO( "[SceneMigration] '{0}': scene schema v{1} -> v{2} - the cloud noise volume is an asset "
                      "now, so {3} bake setting(s) were dropped from {4} entity(ies). Those layers use the "
                      "built-in default volume; pick another in the component's Noise Volume slot. Re-save "
                      "(or run SceneMigrator) to stamp the file so this never runs again.",
                      sceneData->SceneName, kSceneVersionTonemap, kSceneVersionCloudNoise,
                      migration.CloudNoise.FieldsDropped, migration.CloudNoise.Entities );
        }
        if ( migration.CloudSpeciesRaised && migration.CloudSpecies.Entities > 0 )
        {
            LOG_INFO( "[SceneMigration] '{0}': scene schema v{1} -> v{2} - a cloud layer names a SPECIES now, "
                      "so {3} field(s) were dropped from {4} entity(ies) and {5} of them had their scalar "
                      "cloud type translated into one. The layer's shell is computed from the species' own "
                      "altitudes and is no longer authored. Re-save (or run SceneMigrator) to stamp the file "
                      "so this never runs again.",
                      sceneData->SceneName, kSceneVersionCloudNoise, kSceneVersionCloudSpecies,
                      migration.CloudSpecies.FieldsDropped, migration.CloudSpecies.Entities,
                      migration.CloudSpecies.SpeciesSet );
        }
        if ( migration.CloudTypeRaised && migration.CloudType.Entities > 0 )
        {
            LOG_INFO( "[SceneMigration] '{0}': scene schema v{1} -> v{2} - the kind of cloud a layer is made "
                      "of is an ASSET now, so {3} entity(ies) were touched: {4} had their species turned "
                      "into a .decloudtype handle, {5} named a noise volume the layer no longer carries "
                      "(the cloud type carries it - see the warning above for which), and {6} had a species "
                      "value that could not be read at all. Re-save (or run SceneMigrator) to stamp the file "
                      "so this never runs again.",
                      sceneData->SceneName, kSceneVersionCloudSpecies, kSceneVersionCloudType,
                      migration.CloudType.Entities, migration.CloudType.TypesSet, migration.CloudType.VolumesLost,
                      migration.CloudType.FieldsBroken );
        }
        if ( migration.CloudSetRaised && migration.CloudSet.Entities > 0 )
        {
            LOG_INFO( "[SceneMigration] '{0}': scene schema v{1} -> v{2} - a cloud layer carries a SET of up "
                      "to {3} kinds of cloud now instead of one, so {4} entity(ies) were touched and {5} "
                      "cloud type(s) moved into the first slot ({6} of them empty). The sky is unchanged: "
                      "the union of a one-element set is that element, and the first slot reads the same "
                      "placement field the single slot did. Drop a second type into Cloud Type 2 to put two "
                      "kinds of cloud in one sky. Re-save (or run SceneMigrator) to stamp the file so this "
                      "never runs again.",
                      sceneData->SceneName, kSceneVersionCloudType, kSceneVersionCloudSet,
                      Graphic::kCloudSpeciesSlots, migration.CloudSet.Entities, migration.CloudSet.SlotsCarried,
                      migration.CloudSet.SlotsEmpty );
        }
        if ( migration.TerrainMaterialRaised && migration.TerrainMaterial.Entities > 0 )
        {
            // The names, not just the counts. This is the one migration in this file that DROPS values
            // rather than moving them (a material's new home is a `.demat`, and a pure function cannot
            // write one — see MigrateTerrainMaterialV6ToV7), so the log has to be good enough to re-author
            // from. DC 1.4: never substitute a default quietly.
            std::string dropped;
            for ( const auto& name : migration.TerrainMaterial.DroppedNames )
            {
                if ( !dropped.empty() )
                    dropped += ", ";
                dropped += name;
            }

            LOG_INFO( "[SceneMigration] '{0}': scene schema v{1} -> v{2} - the terrain's material is a "
                      "`.demat` now, named by Terrain > Material, so the inline Material component {3} "
                      "terrain entity(ies) carried was removed. It held {4} parameter(s) and {5} texture(s): "
                      "{6}. Re-author them on a terrain material (Details > Terrain > New Terrain Material, "
                      "then Edit) - they are not read any more. Re-save (or run SceneMigrator) to stamp the "
                      "file so this never runs again.",
                      sceneData->SceneName, kSceneVersionCloudSet, kSceneVersionTerrainMaterial,
                      migration.TerrainMaterial.Entities, migration.TerrainMaterial.Params,
                      migration.TerrainMaterial.Textures, dropped.empty() ? "nothing nameable" : dropped );
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

        // WHICH entity each record becomes, which one its payload lands on and what it hangs off is a pure
        // function of the parsed tree, and it lives in Rules::PlanSceneStitch so a test can call it: this
        // file cannot be compiled without the renderer, so for as long as the stitch was written out here
        // it was unreachable by every suite in the repository and a defect planted in it stayed green.
        // What remains below is the part only the loader can do — make the entities and feed the payloads.
        // InstantiatedLater: in a .desce a PrefabPath record names another FILE, and the entity it becomes
        // is made by pass 3 below out of that file - so it is listed here, not created.
        const Rules::StitchPlan plan = Rules::PlanSceneStitch( sceneData->Entities, &Common::UUID::Generate,
                                                               Rules::PrefabRecordPolicy::InstantiatedLater );

        // DC 1.4: a file that names one id twice, or names a parent that is not in it, loads as a scene
        // that is quietly missing pieces. Say which, once, instead of leaving it to be found in the viewport.
        if ( plan.Shadowed > 0 || plan.UnresolvedParents > 0 )
        {
            LOG_WARN( "[SceneSerializer] '{0}': {1} entity record(s) claim an id another record already "
                      "claimed (their payload is written onto the first claimant and their own entity stays "
                      "bare), and {2} parent link(s) name an entity this file does not contain. {3} id(s) "
                      "were minted for records that carried none.",
                      sceneData->SceneName, plan.Shadowed, plan.UnresolvedParents, plan.Minted );
        }

        std::unordered_map<Common::UUID, ECS::Entity> entityMap;

        // Pass 1 — create normal entities
        std::vector<ECS::Entity> created;
        created.reserve( plan.Created.size() );
        for ( const auto& plannedEntity : plan.Created )
        {
            const Assets::EntityData& entityData = sceneData->Entities[plannedEntity.Record];
            ECS::Entity               entity =
                 m_Scene->CreateEntityWithUUID( plannedEntity.Id, entityData.Tag.value_or( "Entity" ) );
            created.push_back( entity );
            entityMap.insert( { plannedEntity.Id, entity } );
        }

        // Pass 2 — deserialize normal entities and wire up hierarchy
        for ( const auto& load : plan.Loads )
        {
            ECS::Entity entity = created[load.Target];
            Serialize::EntitySerializer::DeserializeEntity( sceneData->Entities[load.Record], entity,
                                                            *m_AssetManager );

            if ( load.Parent != Rules::kNoSlot )
                m_Scene->Attach( created[load.Parent], entity );
        }

        // Pass 3 — instantiate prefab roots and apply their saved transforms
        for ( const auto& plannedPrefab : plan.PrefabRecords )
        {
            const Assets::EntityData* entityData = &sceneData->Entities[plannedPrefab.Record];

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