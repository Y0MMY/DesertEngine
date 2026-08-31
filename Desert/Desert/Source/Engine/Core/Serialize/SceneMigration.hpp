#pragma once

#include <Engine/Assets/Prefab/PrefabData.hpp>

#include <Common/Core/Constants.hpp>

#include <filesystem>
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
    //   3             - the cloud noise volume is an ASSET, so the four bake settings are gone from the
    //                   component and from the file
    //   4             - a cloud layer names a SPECIES. The scalar cloud type, its variance and the two
    //                   fields that stated the shell by hand are gone: the species carries its altitudes
    //                   and the shell is computed from them
    //   5             - the species is an ASSET. The enumerator becomes a handle to a `.decloudtype`, and
    //                   the layer's own noise-volume slot moves into that type
    //   6             - a layer carries a SET of up to four kinds of cloud instead of one. `CloudType`
    //                   becomes `CloudType1`, the first of four slots; nothing else about a scene changes
    //                   and a one-type layer renders the sky it rendered before
    //   7             - the terrain's material is a `.demat` named by `Terrain.Material`, so the
    //                   `Material` component a terrain entity used to carry as its authoring channel is
    //                   gone from terrain entities
    //   8             - a material is named by a path RELATIVE to the assets root, like the three cloud
    //                   asset classes already were. The absolute paths the saver used to write carried one
    //                   developer's home directory into every scene in the repository
    inline constexpr int kSceneVersionSky             = 1;
    inline constexpr int kSceneVersionTonemap         = 2;
    inline constexpr int kSceneVersionCloudNoise      = 3;
    inline constexpr int kSceneVersionCloudSpecies    = 4;
    inline constexpr int kSceneVersionCloudType       = 5;
    inline constexpr int kSceneVersionCloudSet        = 6;
    inline constexpr int kSceneVersionTerrainMaterial = 7;
    inline constexpr int kSceneVersionMaterialPath    = 8;
    inline constexpr int kSceneVersion                = kSceneVersionMaterialPath;

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
    // (Ten of the eleven repository scenes were moved that way, by this same task. The eleventh,
    // Fog_Showcase, was measured breaking under ACES because its EXPOSURE was authored for Reinhard, and
    // so it stays on Reinhard until it is re-exposed - which is the case the operator is a per-scene
    // property for in the first place. Numbers in Docs/Clouds/CALIBRATION.md, T-ACES section.)
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

    // What MigrateCloudNoiseV2ToV3 removed, returned rather than logged, for the same reason as the three
    // above.
    struct CloudNoiseMigrationReport
    {
        int Entities      = 0; // entities carrying a "VolumetricCloud" payload that was touched
        int FieldsDropped = 0; // individual bake settings deleted (0..4 per entity)
    };

    // Raises a scene from schema v2 to v3: deletes "WeatherSeed", "WeatherOctaves", "DetailSeed" and
    // "DetailOctaves" from every "VolumetricCloud" payload.
    //
    // WHY DELETE AND NOT CARRY. Those four numbers parameterised a GPU bake that no longer exists; the
    // noise volume is an asset now, and the seed and periods that make one live in the volume's own header
    // (Engine/Assets/CloudNoiseVolume.hpp). There is nowhere on the component to carry them TO. Turning
    // each scene's seed into a freshly baked volume would have been the other option and was rejected: a
    // 128^3 bake costs tens of seconds, it would have written an 8 MiB file per scene for a value nobody
    // authored deliberately, and the volumes would differ between scenes for no reason an artist could see.
    // The scenes therefore adopt the default volume, and the entity keeps every parameter that still means
    // something.
    //
    // Left BEHIND on purpose: the new "NoiseVolume" slot is not written. An absent key is how the
    // reflection serializer spells "keep the C++ default", and the C++ default is an empty handle, which is
    // exactly "use the built-in default volume". Writing a path here would invent a choice for the artist.
    //
    // PURE - no GPU, no filesystem, no global state, and not even a log line: the counters go back to the
    // loader, which is the one that knows which file this was.
    //
    // Idempotent: a payload with none of the four keys is left byte-identical and reports zero.
    //
    // SHELF LIFE: this raises v2 to v3 and nothing else. It is deleted once no v2 file remains.
    CloudNoiseMigrationReport MigrateCloudNoiseV2ToV3( std::vector<Assets::EntityData>& entities );

    // What MigrateCloudSpeciesV3ToV4 did to one file.
    struct CloudSpeciesMigrationReport
    {
        int Entities      = 0; // entities carrying a "VolumetricCloud" payload that was touched
        int FieldsDropped = 0; // "LayerBottomAltitude", "LayerThickness", "CloudTypeVariance" (0..3 each)
        int SpeciesSet    = 0; // entities whose old scalar "CloudType" became a named species
    };

    // Raises a scene from schema v3 to v4: the vertical profile stopped being one analytic curve driven by
    // a scalar and became a per-SPECIES table, so the file has to name a species instead of a number.
    //
    // WHAT IS DROPPED AND WHY NOTHING IS CARRIED FROM IT.
    //
    //   * "LayerBottomAltitude", "LayerThickness" - the shell is now the union of the altitude ranges of
    //     the species in the layer and is computed by Graphic::PackCloudParams. An authored shell and a
    //     species' own altitudes are two numbers obliged to agree, which is the defect class this whole
    //     move removes; carrying the authored pair forward would reintroduce it under a new name.
    //   * "CloudTypeVariance" - it mixed noise into the scalar so that neighbouring clouds would not all
    //     reach the same ceiling. The table's second axis is the placement pattern's own value, which
    //     answers the same question with height that CORRELATES with how much cloud is there. There is
    //     nothing on the component for it to become.
    //
    // WHAT IS CARRIED. "CloudType" was a scalar from "flat sheet low in the layer" to "tall heaped
    // cloud", and the library is ordered along exactly that axis, so the scalar picks the species by
    // quarters: below 0.25 Stratus, below 0.55 CumulusMediocris, below 0.85 CumulusCongestus, and above
    // it Cumulonimbus. The boundaries are placed so that the component's own former default of 0.6 lands
    // on CumulusCongestus, which is the species the new default names - a scene that carried the default
    // therefore comes out of the migration looking like what it was.
    //
    // A payload with no "CloudType" at all keeps the C++ default by NOT writing a species: an absent key
    // is how the reflection serializer spells "leave it alone", and inventing Stratus for a file that
    // never said anything would be a guess about intent.
    //
    // PURE - no GPU, no filesystem, no global state. The counters go back to the loader, which is the one
    // that knows which file this was.
    //
    // Idempotent: a payload with none of the four keys is left byte-identical and reports zero.
    //
    // SHELF LIFE: this raises v3 to v4 and nothing else. It is deleted once no v3 file remains.
    CloudSpeciesMigrationReport MigrateCloudSpeciesV3ToV4( std::vector<Assets::EntityData>& entities );

    // What MigrateCloudTypeV4ToV5 did to one file.
    struct CloudTypeMigrationReport
    {
        int Entities     = 0; // entities carrying a "VolumetricCloud" payload that was touched
        int TypesSet     = 0; // entities whose "Species" enumerator became a "CloudType" asset handle
        int VolumesLost  = 0; // entities that named a noise volume the component no longer carries
        int FieldsBroken = 0; // "Species" values that were not a usable integer - the layer keeps default
    };

    // Raises a scene from schema v4 to v5: the kind of cloud a layer is made of stopped being an
    // enumerator compiled into the engine and became an ASSET an artist can author, name and ship.
    //
    // WHAT IS CARRIED, AND WHY IT IS EXACT RATHER THAN APPROXIMATE. The four enumerators of
    // the deleted Graphic::CloudSpecies - stratus, cumulus mediocris, cumulus congestus, cumulonimbus -
    // ship as four files under Resources/Assets/Clouds/Types carrying the SAME twelve numbers T0 compiled
    // in. So the migration is a rename, not a reinterpretation: what it writes is the PATH of that file,
    // relative to the assets root, which is the form Core::MakeAssetResolver reads an asset field back
    // from — and it is composed from Assets::CloudTypeAssetRelativePath rather than spelt out, so this
    // function stays pure and cannot disagree with the directory the preloader scans.
    // Desert/Tests/Engine/CloudType asserts that the four files hold T0's numbers. A scene therefore comes
    // out of this migration rendering the sky it went in with.
    //
    // WHAT IS DROPPED. "NoiseVolume" - the layer's own slot for the 3D noise. It moved onto the cloud TYPE,
    // because the character of a cloud's edge is a property of the kind of cloud rather than of the weather
    // it is having, and the component must not keep a second copy (§4.2). A scene that named one cannot be
    // carried automatically: the handle it holds identifies a `.dcnv`, and turning that into a NEW cloud
    // type file would mean this function writing to disk, which the contract's "migration is a pure
    // function" forbids for good reason. Every such entity is COUNTED and named in the loader's log so the
    // artist is told which layer to re-point, rather than finding out from a sky that lost its edge. No
    // scene in this repository carries one, which is what makes the loud drop affordable.
    //
    // PURE - no GPU, no filesystem, no global state. The counters go back to the loader, which is the one
    // that knows which file this was.
    //
    // Idempotent: a payload with neither key is left byte-identical and reports zero.
    //
    // SHELF LIFE: this raises v4 to v5 and nothing else. It is deleted once no v4 file remains.
    CloudTypeMigrationReport MigrateCloudTypeV4ToV5( std::vector<Assets::EntityData>& entities );

    // What MigrateCloudSetV5ToV6 did to one file.
    struct CloudSetMigrationReport
    {
        int Entities     = 0; // entities carrying a "VolumetricCloud" payload that was touched
        int SlotsCarried = 0; // "CloudType" keys that became "CloudType1"
        int SlotsEmpty   = 0; // of those, the ones whose value was the empty handle
    };

    // Raises a scene from schema v5 to v6: a layer stopped carrying ONE kind of cloud and started carrying
    // a SET of up to four.
    //
    // WHAT IT DOES, AND WHY IT IS A RENAME AND NOTHING MORE. The single `CloudType` key becomes
    // `CloudType1`, the first of four slots, and the other three are left absent — which the component
    // reads as the empty handle they default to. A scene that named one kind of cloud comes out of this
    // naming the same kind of cloud in the first slot, and renders the sky it went in with: the union of a
    // one-element set is that element, and the slot's placement field is the one T1 read (slot 0 takes the
    // zero decorrelation offset for exactly this reason).
    //
    // WHY THE KEY MOVES AT ALL, when leaving it named `CloudType` would have been a migration of nothing.
    // Because four slots that are not called the same thing is the point: `CloudType` beside `CloudType2`,
    // `CloudType3` and `CloudType4` reads as one field of a different kind sitting next to three of
    // another, and the first person to add a fifth would have had to guess which end it belonged at. The
    // rename costs one function and makes the set look like a set in the file as well as in the panel.
    //
    // PURE - no GPU, no filesystem, no global state. The counters go back to the loader, which is the one
    // that knows which file this was.
    //
    // Idempotent: a payload with no "CloudType" key is left byte-identical and reports zero.
    //
    // SHELF LIFE: this raises v5 to v6 and nothing else. It is deleted once no v5 file remains.
    CloudSetMigrationReport MigrateCloudSetV5ToV6( std::vector<Assets::EntityData>& entities );

    // What MigrateTerrainMaterialV6ToV7 did to one file.
    struct TerrainMaterialMigrationReport
    {
        int Entities = 0; // terrain entities that carried a "Material" component, and no longer do
        int Params   = 0; // parameter values those components held
        int Textures = 0; // texture bindings those components held

        // The names of everything above, in the order it was found, so the loader can print WHAT has to be
        // re-authored rather than only how much. A count alone would make this a silent default in all but
        // arithmetic (DC 1.4): "3 values dropped" tells nobody that the grass texture was one of them.
        std::vector<std::string> DroppedNames;
    };

    // Raises a scene from schema v6 to v7: the terrain's material stopped being an ECS::MaterialComponent
    // authored on the terrain entity and became a `.demat` that `Terrain.Material` names by handle.
    //
    // WHAT IT DOES. For every entity carrying BOTH a "Terrain" and a "Material" payload, the "Material"
    // payload is removed and everything it held is reported by name. An entity with a "Material" and no
    // "Terrain" is left alone: there the component is the runtime/Lua `setMaterialParam` channel and the
    // legacy-scene compatibility path, which is all it claims to be and all it now is.
    //
    // WHY IT REMOVES RATHER THAN CARRIES. The new form of these values is a file — a `.demat` with the
    // Terrain shader and these parameters in it — and this function is pure: no filesystem, so it cannot
    // create one, and there is no second place in the scene tree for a material's values to live that is
    // not the inline authoring this task exists to delete. Writing them anywhere else would be the
    // "deprecated but still read" shape of DC 4.1 wearing a migration's clothes.
    //
    // So it drops them, LOUDLY: every name goes back to the loader, which prints them with the scene and
    // tells the reader to re-author them on a terrain material. That is the honest trade and it is a small
    // one — the values are three splat textures and a tint, they were only ever reachable through one
    // editor widget, and no scene in this repository has any (the sweep in the migration's own test suite
    // is what keeps that true).
    //
    // PURE - no GPU, no filesystem, no global state. The counters go back to the loader, which is the one
    // that knows which file this was.
    //
    // Idempotent: an entity with no "Material" payload is left byte-identical and reports zero.
    //
    // SHELF LIFE: this raises v6 to v7 and nothing else. It is deleted once no v6 file remains.
    TerrainMaterialMigrationReport MigrateTerrainMaterialV6ToV7( std::vector<Assets::EntityData>& entities );

    // What MigrateMaterialPathV7ToV8 did to one file.
    struct MaterialPathMigrationReport
    {
        int Entities = 0; // entities in which at least one material path was rewritten
        int Paths    = 0; // individual path strings rewritten to the assets-root-relative form

        // Paths that could NOT be made relative because they do not lie under the assets root at all, and
        // their entity's tag. Left exactly as they were - a file genuinely outside the project has no
        // project-relative form to have - and NAMED, because a scene that carries one still does not open
        // on another machine and a count alone would not say which slot to re-point (DC 1.4).
        std::vector<std::string> OutsideNames;
    };

    // Raises a scene from schema v7 to v8: a material stops being named by the ABSOLUTE path the saver
    // wrote and is named relative to the assets root, which is the form Core::MakeAssetResolver reads back.
    //
    // WHY. `MakeAssetResolver::ToPath`'s MaterialAsset branch wrote `asset->GetMetadata().Filepath` verbatim.
    // With a project open every content root is absolute (Constants::Path::SetProjectRoot), so a scene
    // re-saved in the editor took whoever saved it home directory into the repository: 22 distinct
    // `/Users/<somebody>/.../Materials/*.demat` strings across 42 of the 51 scenes shipped here, none of
    // which names anything on any other machine. The three cloud asset classes went relative for exactly
    // this reason and say so at their branches; this applies the decision already taken to the fourth.
    //
    // WHAT IT REWRITES. The four places a scene can name a material: `MaterialPaths` on `StaticMesh`,
    // `InstancedStaticMesh` and `SkinnedMesh`, and `Material` on `Terrain`.
    //
    // HOW, WITHOUT A FILESYSTEM. std::filesystem::relative() consults the disk (it canonicalises both
    // sides), and this function may not. So the rewrite is LEXICAL: the path's components are searched for
    // the LAST occurrence of `assetsRoot`'s own component sequence, and everything after it is kept.
    // That deliberately makes the answer independent of how the root is spelled - `Resources/Assets/` and
    // `/Users/x/Proj/Editor/Resources/Assets/` both reduce
    // `/Users/x/Proj/Editor/Resources/Assets/Materials/M.demat` to `Materials/M.demat` - which is what lets
    // the editor (working directory `Editor/`) and Tools/SceneMigrator (working directory the repository
    // root) produce the same file from the same input.
    //
    // The root is a PARAMETER and not `Constants::Path::ASSETS_PATH` read from inside, so the function has
    // no global to disagree with and a test can drive it with a root of its own.
    //
    // Idempotent: a path already relative to the root contains no `assetsRoot` sequence to strip, so a
    // second run leaves the tree byte-identical and reports zero. An empty string (which is what ToPath
    // writes for a slot whose handle resolves to nothing) is left alone rather than turned into ".".
    //
    // PURE - no GPU, no filesystem, no global state. The counters go back to the loader, which is the one
    // that knows which file this was.
    //
    // SHELF LIFE: this raises v7 to v8 and nothing else. It is deleted once no v7 file remains.
    MaterialPathMigrationReport MigrateMaterialPathV7ToV8( std::vector<Assets::EntityData>& entities,
                                                           const std::filesystem::path&     assetsRoot );

    // Everything that ran, so the caller can say which scene moved and how far.
    struct SceneMigrationReport
    {
        bool                   SkyRaised = false; // the sky schema was below kSceneVersionSky
        SkyMigrationReport     Sky;
        bool                   UnitsRaised = false; // the world unit was below kUnitVersion
        UnitMigrationReport    Units;
        bool                   TonemapperRaised = false; // the schema was below kSceneVersionTonemap
        TonemapMigrationReport Tonemap;
        bool                      CloudNoiseRaised = false; // the schema was below kSceneVersionCloudNoise
        CloudNoiseMigrationReport CloudNoise;
        bool                        CloudSpeciesRaised = false; // the schema was below kSceneVersionCloudSpecies
        CloudSpeciesMigrationReport CloudSpecies;
        bool                        CloudTypeRaised = false; // the schema was below kSceneVersionCloudType
        CloudTypeMigrationReport    CloudType;
        bool                        CloudSetRaised = false; // the schema was below kSceneVersionCloudSet
        CloudSetMigrationReport     CloudSet;
        // the schema was below kSceneVersionTerrainMaterial
        bool                           TerrainMaterialRaised = false;
        TerrainMaterialMigrationReport TerrainMaterial;
        bool                        MaterialPathRaised = false; // the schema was below kSceneVersionMaterialPath
        MaterialPathMigrationReport MaterialPath;

        bool Changed() const
        {
            return SkyRaised || UnitsRaised || TonemapperRaised || CloudNoiseRaised || CloudSpeciesRaised ||
                   CloudTypeRaised || CloudSetRaised || TerrainMaterialRaised || MaterialPathRaised;
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
    //
    // `assetsRoot` is what the v7 -> v8 material-path step measures against, and it DEFAULTS to the live
    // content root so that the loader, the migrator tool and the six suites that already call this need no
    // change. The default is evaluated at the call site, which is the only place that knows whether a
    // project has been opened; the step underneath it takes the root explicitly and is tested that way.
    SceneMigrationReport
    MigrateScene( SceneSerialized&             scene,
                  const std::filesystem::path& assetsRoot = Common::Constants::Path::ASSETS_PATH );

} // namespace Desert::Core
