#pragma once

#include <Engine/Assets/Prefab/PrefabData.hpp>

#include <optional>
#include <string>
#include <vector>

namespace Desert::Core
{
    // Schema generation of a .desce file, and what each step of it means:
    //
    //   absent (or 0) - written while the procedural sky still lived inside SkyboxComponent
    //   1             - the sky lives in its own "SkyAtmosphere" payload
    //   2             - the tonemapping operator is a scene property, and the file states which one
    //
    // Each step has its OWN constant and each migration is gated on its own, not on kSceneVersion. Gating
    // them all on the head would re-run every earlier migration the moment the head moved: raising the
    // head to 2 would have sent every v1 file in the repository back through the sky migration and
    // reported a schema move that did not happen.
    //
    // This is deliberately a SECOND integer and not UnitVersion. UnitVersion's contract is written beside
    // it below - "bump this only if the world unit changes again" - so reusing it would couple two
    // migrations that have nothing to do with each other: an old metres-era scene would be declared
    // sky-migrated the moment someone re-saved it for units, and vice versa.
    inline constexpr int kSceneVersionSky     = 1;
    inline constexpr int kSceneVersionTonemap = 2;
    inline constexpr int kSceneVersion        = kSceneVersionTonemap;

    // World-unit generation of a .desce file. Absent (or 0) means the scene was authored when one world
    // unit was one METRE; today a unit is a CENTIMETRE (Common/Core/Units.hpp), so such a scene is scaled
    // x100 once - see MigrateMetresToUnits(). Bump this only if the world unit changes again.
    inline constexpr int kUnitVersion = 1;

    // The on-disk shape of a .desce file, and the ONLY definition of it: the loader parses into this, the
    // saver writes it, and the migrations rewrite it in place. It lives here rather than inside
    // SceneSerializer.cpp because a migration whose input is "the parsed tree" needs the tree's type, and
    // a second copy of this struct anywhere is a format that can silently fork.
    struct SceneSerialized
    {
        std::string                     SceneName;
        std::vector<Assets::EntityData> Entities;
        // Scene-wide settings - reflected, so the whole block round-trips through the generic serializer.
        std::optional<rfl::Generic> Settings;
        std::optional<int>          UnitVersion;
        std::optional<int>          SceneVersion;
    };

    // Number of reflected fields on ECS::SkyAtmosphereData. The migration needs it to say how many NEW
    // fields were left at their C++ default, and it must not reach for the reflection registry to find out
    // (that is a global, and this function is pure). So the number is stated here and a test asserts it
    // against the registry - which turns "a field was added to the component" into a failing test instead
    // of a silently wrong counter.
    // 47 = the 24 fields of the artistic-gradient era + the 22 physical-atmosphere fields (SkyModel and
    // the Physical Atmosphere / Rayleigh / Mie / Absorption / Art Direction groups) added 2026-08-14,
    // + Aerial Perspective Distance, which the camera aerial-perspective volume added 2026-08-15.
    inline constexpr int kSkyAtmosphereFieldCount = 47;

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
    // file - has nowhere to put the old values, so a migration over the live ECS scene would find them
    // already gone.
    //
    // Idempotent: an entity that already has a "SkyAtmosphere" payload is skipped untouched, so a second
    // run reports all zeros and leaves the tree byte-identical.
    //
    // SHELF LIFE: this function upgrades v0 to v1 and nothing else. It is not "support for the old format";
    // when v2 arrives it gets its own v1 -> v2 function, and this one is deleted once no v0 file remains.
    SkyMigrationReport MigrateSkyV0ToV1( std::vector<Assets::EntityData>& entities );

    // What MigrateMetresToUnits touched, returned rather than logged, for the same reason as above: the
    // function is pure and the caller is the one who knows which file this was.
    struct UnitMigrationReport
    {
        int Entities = 0; // entities that had at least one number rescaled
        int Values   = 0; // individual numbers multiplied by 100 (a vec3 counts as one)
        int Rejected = 0; // fields present but unusable (not a finite number / wrong arity) - left alone
    };

    // Raises a metres-era scene to centimetre world units: every LENGTH the scene file owns is multiplied
    // by 100. PURE - no GPU, no filesystem, no global state; a LOG_WARN per rejected value is the only
    // side effect beyond the arguments.
    //
    // It runs on the PARSED TREE, like the sky migration and unlike the version that used to live in
    // SceneSerializer.cpp and walked the live ECS scene AFTER the load. Three things follow from that, and
    // all three are deliberate:
    //
    //  1. Only keys that are PRESENT are scaled. The live version scaled the TransformComponent every
    //     entity is created with, so an entity whose file entry carries no "Scale" - every UI entity in
    //     MainMenu.desce, for one - had its default (1,1,1) turned into (100,100,100). A default is not a
    //     length somebody authored in metres.
    //  2. Prefab CONTENTS are not touched. The scene file owns where a prefab was placed; the prefab file
    //     owns what is inside it. The live version scaled the instantiated children too, so the same
    //     prefab came out a hundred times bigger in an old scene than in a new one.
    //  3. The values are already in centimetres before a single component is deserialized, so nothing
    //     downstream can observe the metres-era numbers.
    //
    // Idempotent by construction: it does not decide anything from the values themselves, so running it on
    // a scene that has already been raised is a second x100 - which is exactly why the caller must gate it
    // on UnitVersion, and why MigrateScene() below is the only supported way to call it.
    //
    // SHELF LIFE: this raises UnitVersion 0 to 1 and nothing else. When the world unit changes again it
    // gets a 1 -> 2 successor; this function is deleted once no unstamped file remains.
    UnitMigrationReport MigrateMetresToUnits( std::vector<Assets::EntityData>& entities,
                                              std::optional<rfl::Generic>&     settings );

    // What MigrateTonemapperV1ToV2 did, returned rather than logged, for the same reason as the two
    // above: the function is pure and only the caller knows which file this was.
    struct TonemapMigrationReport
    {
        bool SettingsCreated = false; // the file carried no "Settings" block at all - one was made for it
        bool OperatorPinned  = false; // "Tonemapper" was written; false when the file already stated one
    };

    // Raises a scene from schema v1 to v2: writes the tonemapping operator the file was AUTHORED under
    // into its settings block. PURE - no GPU, no filesystem, no global state; a LOG_WARN on a settings
    // payload that is not an object is the only side effect beyond the argument.
    //
    // WHY IT WRITES REINHARD AND NOT THE NEW DEFAULT. Decision D-10 made ACES the default operator. A
    // default change is the one change that silently rewrites every file which never mentioned the
    // setting: an absent key means "the C++ default", so on the day the default moved, every existing
    // scene would have been re-graded through a curve its author never chose, with nothing in the file
    // to say so. Exposure and White Point in those scenes were dialled in by eye on extended Reinhard -
    // the cloud demo carries an exposure of 0.22 against a sun of 22 - so this is not a subtle drift, it
    // is the whole grade. Pinning Reinhard makes the operator EXPLICIT at the value that leaves each
    // picture exactly as it was, and moving a scene to ACES then becomes a deliberate, visible edit.
    // (The repository's own scenes were moved that way, by this same task - see Docs/Clouds/CALIBRATION.md.)
    //
    // A scene with no "Settings" block at all - MainMenu.desce is one - gets a block containing only the
    // operator. Every other field stays absent, which is how the reflection serializer spells "keep the
    // C++ default", so no value is invented for it.
    //
    // Idempotent: a tree that already states an operator is left byte-identical, whichever operator that
    // is. A scene may be re-read (undo, a second load) after it was raised, and overwriting an operator
    // the user has since chosen would be worse than not migrating at all.
    //
    // SHELF LIFE: this raises v1 to v2 and nothing else. It is not "support for the old format"; when v3
    // arrives it gets its own v2 -> v3 successor, and this one is deleted once no v1 file remains.
    TonemapMigrationReport MigrateTonemapperV1ToV2( std::optional<rfl::Generic>& settings );

    // Everything that ran, so the caller can say which scene moved and how far.
    struct SceneMigrationReport
    {
        bool                   SkyRaised = false; // the sky schema was below kSceneVersionSky
        SkyMigrationReport     Sky;
        bool                   UnitsRaised = false; // the world unit was below kUnitVersion
        UnitMigrationReport    Units;
        bool                   TonemapperRaised = false; // the schema was below kSceneVersionTonemap
        TonemapMigrationReport Tonemap;

        bool Changed() const
        {
            return SkyRaised || UnitsRaised || TonemapperRaised;
        }
    };

    // Raises a parsed scene file to the current generation of BOTH version integers and stamps them, so a
    // tree that has been through this function is one nothing will migrate again. This is the single entry
    // point: the loader calls it on the tree it just parsed, and SceneMigrator calls it on every .desce in
    // the repository and writes the result back - the contract's "data migrates once, and is written back
    // in the new form" is only true if something actually writes it back.
    //
    // The three migrations are independent (no sky field is a length, no length lives under
    // "SkyAtmosphere", and the tonemapper touches neither), so the order below is the order they were
    // written and nothing depends on it.
    SceneMigrationReport MigrateScene( SceneSerialized& scene );

} // namespace Desert::Core
