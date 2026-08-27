#include "PrefabFactory.hpp"
#include <Engine/ECS/Components.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Core/Serialize/EntitySerializer.hpp>
#include <Engine/Core/Serialize/SceneStitchRules.hpp>

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

        const std::vector<Assets::EntityData>& records = prefab.GetEntities();

        // The identity stitch is Rules::PlanSceneStitch, the same function the scene loader plans with —
        // this used to be a hand-written copy of it, and the copy had drifted: it registered records with
        // `entityMap[id] = e`, so when two records in one prefab claimed one id the LAST one silently took
        // it over, while the scene loader's copy gave it to the FIRST. Nothing compiled either file, so
        // neither answer was ever anybody's decision. It is one decision now, and it is documented where
        // it is taken.
        //
        // CreatedInPlace: unlike a .desce, a record here that carries a PrefabPath is still an entity of
        // THIS prefab — one with another prefab nested underneath it — so it is created and stitched like
        // any other, and the nested body is hung off it below.
        const Core::Rules::StitchPlan plan = Core::Rules::PlanSceneStitch(
             records, &Common::UUID::Generate, Core::Rules::PrefabRecordPolicy::CreatedInPlace );

        // DC §1.4: no silent fallback. Two records of one prefab claiming one id means the second one's
        // components land on the first one's entity and its own entity stays bare — say which prefab and
        // how many, once.
        //
        // UnresolvedParents is deliberately NOT reported: a prefab's first record keeps the `parent` it had
        // in the scene it was cut from, and that entity is by definition not in the prefab. Here "the
        // parent is not in this file" is the normal spelling of "this record is a root of the instance",
        // and warning about it would fire on every well-formed prefab in existence.
        if ( plan.Shadowed > 0 )
        {
            LOG_WARN( "[PrefabFactory] '{0}': {1} record(s) claim an id another record in the same prefab "
                      "already claimed. Their components are applied to the first claimant and their own "
                      "entities are left bare.",
                      prefab.GetMetadata().Filepath.string(), plan.Shadowed );
        }

        // 1. Create all entities with FRESH UUIDs to avoid collisions when multiple instances exist. The id
        // the record carries is a LINK KEY inside this file only — PlannedEntity::Id is what resolved the
        // parent links above, and it never reaches the scene.
        std::vector<ECS::Entity> created;
        created.reserve( plan.Created.size() );
        for ( const auto& plannedEntity : plan.Created )
        {
            created.push_back( scene.CreateEntityWithUUID(
                 Common::UUID::Generate(), records[plannedEntity.Record].Tag.value_or( "PrefabEntity" ) ) );
        }

        // 2. Nested prefab bodies, hung off the entity the nesting record became.
        for ( const auto& plannedPrefab : plan.PrefabRecords )
        {
            const Assets::EntityData& data = records[plannedPrefab.Record];

            auto nested = assetManager.FindByPath<Assets::PrefabAsset>( *data.PrefabPath );
            if ( !nested )
                continue;

            ECS::Entity nestedRoot = Instantiate( *nested, scene, assetManager, stack );
            scene.Attach( created[plannedPrefab.Slot], nestedRoot );
        }

        // 3. Apply components and set up hierarchy.
        for ( const auto& load : plan.Loads )
        {
            ECS::Entity entity = created[load.Target];
            Core::Serialize::EntitySerializer::DeserializeEntity( records[load.Record], entity, assetManager );

            if ( load.Parent != Core::Rules::kNoSlot )
                scene.Attach( created[load.Parent], entity );
        }

        // The root is the FIRST record, because PrefabAsset::CreateFromEntity writes the subtree pre-order
        // and so the first record is the entity the prefab was cut from.
        ECS::Entity rootEntity = created.front();

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
