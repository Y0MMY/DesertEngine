#pragma once

#include <Common/Core/UUID.hpp>
#include <Engine/Assets/Prefab/PrefabData.hpp>

#include <cstddef>
#include <span>
#include <unordered_map>
#include <vector>

namespace Desert::Core::Rules
{
    // The DECISIONS the scene loader makes while turning a parsed .desce tree into entities, as a pure
    // function of that tree.
    //
    // WHY THIS IS NOT SIMPLY LEFT IN SceneSerializer::DeserializeFromJson. Because nothing could reach it
    // there. SceneSerializer.cpp includes Scene.hpp, Scene.hpp reaches the renderer, and no test project in
    // this repository compiles or links that file - which means the two-pass identity stitch, the code every
    // single scene load in the engine goes through, was invisible to a green sweep. A defect deliberately
    // planted in it left all 69 suites passing. The stitch is the part worth testing; creating entities and
    // handing payloads to the entity serializer is the part that fetches the arguments. Same split as
    // Engine/ECS/System/SystemRules.hpp, and for the same reason.
    //
    // WHY A PLAN AND NOT A CALLBACK. The loader needs THREE things out of this: which entities to create and
    // under which ids, which entity each record's payload lands on, and which entity is its parent. Returned
    // as data, all three can be asserted in a test without a Scene existing; handed to a callback, only the
    // side effects could be, and the side effects need a renderer.

    // "No entity". Used for a parent link that names an entity the file does not contain, which the loader
    // must leave unattached rather than attaching to entity zero.
    inline constexpr size_t kNoSlot = static_cast<size_t>( -1 );

    // One entity the loader must create, in creation order.
    struct PlannedEntity
    {
        size_t       Record = 0;       // index into the record span handed in
        Common::UUID Id;               // the id the entity is created under
        bool         IdMinted = false; // the record carried no `id` and one was minted for it
    };

    // Where one record's payload goes once the entities exist.
    struct PlannedLoad
    {
        size_t Record = 0;       // index into the record span handed in
        size_t Target = 0;       // index into StitchPlan::Created - the entity that receives the payload
        size_t Parent = kNoSlot; // index into StitchPlan::Created, or kNoSlot when the record names none
    };

    struct StitchPlan
    {
        std::vector<PlannedEntity> Created;       // pass 1, in file order over non-prefab records
        std::vector<PlannedLoad>   Loads;         // pass 2, same order and same length as Created
        std::vector<size_t>        PrefabRecords; // records carrying a PrefabPath, in file order

        size_t Minted            = 0; // entities whose id was invented because the file did not name one
        size_t Shadowed          = 0; // records whose id was already claimed - their payload lands on the CLAIMANT
        size_t UnresolvedParents = 0; // records naming a parent no non-prefab record answers to
    };

    // Plans the identity stitch for one parsed scene file. PURE - no GPU, no filesystem, no global state.
    //
    // @p mint is called ONCE for each record that carries no `id`, in file order, and must return a fresh
    // identity (the loader passes Common::UUID::Generate; a test passes a counter so the plan is
    // reproducible).
    //
    // WHY THE ID IS REMEMBERED AND NOT RECOMPUTED. The two passes used to call `id.value_or( Generate() )`
    // independently, so an entity saved without an `id` was minted TWO different ids: pass 2 looked it up
    // under the second, missed, and skipped it. The entity existed in the scene carrying nothing but a tag,
    // and no parent link pointing at it ever resolved. Here the id is decided once, in Created, and pass 2
    // reads it back rather than deriving it a second time - the defect is not fixed, it is unspellable.
    //
    // WHY `insert` SEMANTICS ARE PART OF THE RULE. The loader registers each entity with
    // `unordered_map::insert`, which keeps the FIRST value for a key. So when two records claim one id -
    // a corrupt or hand-merged file - the second record's payload is written onto the first record's
    // entity, and the second entity stays bare. That is the behaviour this function reproduces exactly, and
    // StitchPlan::Shadowed counts it so the loader can say so out loud instead of leaving a silently
    // half-loaded scene. It is NOT quietly repaired here: changing what a corrupt file loads as is a
    // decision about scene data, not about testability.
    //
    // PREFAB ROOTS ARE PLANNED, NOT STITCHED. A record with a PrefabPath is instantiated from another file
    // by PrefabFactory and cannot be created from the record alone, so it is only listed. It is also absent
    // from the id map on purpose: prefab roots are registered by the loader AFTER pass 2, so a plain entity
    // whose parent is a prefab root does not attach - which is what the loader does today.
    template <typename MintFn>
    inline StitchPlan PlanSceneStitch( std::span<const Assets::EntityData> records, MintFn&& mint )
    {
        StitchPlan plan;
        plan.Created.reserve( records.size() );
        plan.Loads.reserve( records.size() );

        // Slot the map kept for each created entity's id: its own, unless another record claimed that id
        // first. Captured at insert time so pass 2 never has to look an id up twice.
        std::vector<size_t> winner;
        winner.reserve( records.size() );

        std::unordered_map<Common::UUID, size_t> byId;

        // Pass 1 - decide identities.
        for ( size_t record = 0; record < records.size(); ++record )
        {
            const Assets::EntityData& data = records[record];
            if ( data.PrefabPath.has_value() )
            {
                plan.PrefabRecords.push_back( record );
                continue;
            }

            PlannedEntity created;
            created.Record   = record;
            created.IdMinted = !data.id.has_value();
            created.Id       = created.IdMinted ? mint() : *data.id;
            if ( created.IdMinted )
                ++plan.Minted;

            const auto inserted = byId.insert( { created.Id, plan.Created.size() } );
            winner.push_back( inserted.first->second );
            plan.Created.push_back( created );
        }

        // Pass 2 - decide where each payload lands and what it hangs off.
        for ( size_t slot = 0; slot < plan.Created.size(); ++slot )
        {
            PlannedLoad load;
            load.Record = plan.Created[slot].Record;
            load.Target = winner[slot];
            if ( load.Target != slot )
                ++plan.Shadowed;

            const Assets::EntityData& data = records[load.Record];
            if ( data.parent.has_value() && !data.parent->IsNull() )
            {
                const auto found = byId.find( *data.parent );
                if ( found != byId.end() )
                    load.Parent = found->second;
                else
                    ++plan.UnresolvedParents;
            }

            plan.Loads.push_back( load );
        }

        return plan;
    }

} // namespace Desert::Core::Rules
