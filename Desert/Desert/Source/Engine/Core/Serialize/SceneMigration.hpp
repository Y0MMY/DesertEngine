#pragma once

#include <Engine/Assets/Prefab/PrefabData.hpp>

#include <vector>

namespace Desert::Core
{
    // Schema generation of a .desce file. Absent (or 0) means the file was written while the procedural
    // sky still lived inside SkyboxComponent; 1 means the sky lives in its own "SkyAtmosphere" payload.
    //
    // This is deliberately a SECOND integer and not UnitVersion. UnitVersion's contract is written next to
    // it in SceneSerializer.cpp - "bump this only if the world unit changes again" - so reusing it would
    // couple two migrations that have nothing to do with each other: an old metres-era scene would be
    // declared sky-migrated the moment someone re-saved it for units, and vice versa.
    inline constexpr int kSceneVersion = 1;

    // Number of reflected fields on ECS::SkyAtmosphereData. The migration needs it to say how many NEW
    // fields were left at their C++ default, and it must not reach for the reflection registry to find out
    // (that is a global, and this function is pure). So the number is stated here and a test asserts it
    // against the registry - which turns "a field was added to the component" into a failing test instead
    // of a silently wrong counter.
    inline constexpr int kSkyAtmosphereFieldCount = 24;

    // What MigrateSkyV0ToV1 actually did, returned rather than logged, so the pure function stays pure and
    // the LOADER is the one that speaks (the counters are meaningless unless someone names the scene).
    //
    // Every reflected field of SkyAtmosphereData ends up in exactly one of the three counters, so
    // FieldsCarried + FieldsDefaulted + FieldsRejected == kSkyAtmosphereFieldCount x Entities.
    struct SkyMigrationReport
    {
        int Entities        = 0; // entities that carried a "Skybox" payload and gained a "SkyAtmosphere" one
        int FieldsCarried   = 0; // mapped fields found in the old payload and copied (or converted)
        int FieldsDefaulted = 0; // new fields with no value to carry - they keep their C++ default
        int FieldsRejected  = 0; // mapped fields present but unusable (wrong JSON type, wrong arity, NaN)
    };

    // Raises the sky half of a scene from schema v0 to v1: reads each entity's old "Skybox" payload and
    // inserts a "SkyAtmosphere" payload beside it. PURE - no GPU, no filesystem, no global state; the only
    // side effect beyond the argument is a LOG_WARN per rejected value, which DC 1.4 requires (a value we
    // silently dropped is a value nobody will ever find again).
    //
    // It runs on the PARSED TREE, before a single entity exists, and that is the whole point: once
    // SkyboxComponent lost its sky fields, the load loop - which iterates the component REGISTRY, not the
    // file - has nowhere to put the old values, so a migration over the live ECS scene (the shape
    // MigrateMetresToUnits uses) would find them already gone.
    //
    // Idempotent: an entity that already has a "SkyAtmosphere" payload is skipped untouched, so a second
    // run reports all zeros and leaves the tree byte-identical.
    //
    // SHELF LIFE: this function upgrades v0 to v1 and nothing else. It is not "support for the old format";
    // when v2 arrives it gets its own v1 -> v2 function, and this one is deleted once no v0 file remains.
    SkyMigrationReport MigrateSkyV0ToV1( std::vector<Assets::EntityData>& entities );

} // namespace Desert::Core
