#pragma once

// THIS IS TOOL CODE, for the reason SceneMigration.hpp gives at greater length: a conversion belongs in
// the one place it can expire — a tool that runs over FILES and writes them back — and the engine's
// loader knows nothing about old formats. It REFUSES a .deprefab that is not at the current generation
// (Engine/Assets/Prefab/PrefabFormat.hpp) and names this tool.
//
// WHAT THIS TOOL KNOWS, AND WHAT IT DELIBERATELY DOES NOT.
//
// A prefab shares the scene's two generation integers (its payload is the scene's own EntityData), but
// unlike scenes it spent its whole life UNSTAMPED: no .deprefab written before Д28 states any version at
// all. So there is exactly one migration step here:
//
//   absent/absent (v0/v0)  ->  stamped at Core::kSceneVersion / Core::kUnitVersion, entities untouched.
//
// The step is stamp-only and says so in its report, because stamping is all that can be done honestly: an
// unstamped file carries no evidence of WHICH old generation its payloads are, so the per-entity scene
// migrations (units x100, material-path rewriting, ...) cannot be gated onto it without guessing. The
// repository ships zero .deprefab files (measured by the PrefabVersionGate corpus test, which sweeps the
// whole tree including untracked files), so the population this assumption covers is "prefabs a user
// saved from a recent build" — current-schema payloads missing only the stamp. A file stamped at any
// OTHER generation was written by a build this tool does not know (most likely a newer one), and it is
// refused rather than guessed at; when Core::kSceneVersion moves next, the step for v11 -> v12 joins
// this file the way the scene steps joined SceneMigration.
//
// The head this tool stamps IS Core::kSceneVersion — read from the engine header, not restated — so the
// migrator and the loader cannot disagree about what "current" means. (SceneMigration needs its
// static_assert because it keeps per-step constants; this tool has one step and no constant to drift.)

#include <Engine/Assets/Prefab/PrefabData.hpp>
#include <Engine/Core/Serialize/SceneFormat.hpp>

#include <string>

namespace Desert::Migration
{
    // What MigratePrefab did (or refused to do), returned rather than logged so the function stays pure
    // and the CALLER is the one that speaks, naming the file.
    struct PrefabMigrationOutcome
    {
        bool Changed        = false; // the tree was stamped and should be written back
        bool AlreadyCurrent = false; // both integers already at the head; nothing to do

        // Non-empty: the file's generation is one this tool has no step for. Nothing was changed.
        std::string Error;

        // What the tree stated before the step (absent = 0), for the caller's report.
        int FoundSceneVersion = 0;
        int FoundUnitVersion  = 0;
    };

    // The one step, as a pure function over the parsed tree: an unversioned prefab is stamped at the
    // current generations IN PLACE; a current one is left alone; anything else is refused via Outcome.
    // Entities are never touched — a test pins that by comparing them before and after.
    [[nodiscard]] PrefabMigrationOutcome MigratePrefab( Assets::PrefabData& prefab );

} // namespace Desert::Migration
