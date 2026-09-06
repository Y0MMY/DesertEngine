#include "PrefabMigration.hpp"

#include <spdlog/fmt/fmt.h>

namespace Desert::Migration
{
    PrefabMigrationOutcome MigratePrefab( Assets::PrefabData& prefab )
    {
        PrefabMigrationOutcome outcome;
        outcome.FoundSceneVersion = prefab.SceneVersion.value_or( 0 );
        outcome.FoundUnitVersion  = prefab.UnitVersion.value_or( 0 );

        if ( outcome.FoundSceneVersion == Core::kSceneVersion && outcome.FoundUnitVersion == Core::kUnitVersion )
        {
            outcome.AlreadyCurrent = true;
            return outcome;
        }

        // The one known step: a file that states NEITHER integer was written before prefabs were
        // versioned at all (Д28). Stamp it, touch nothing else.
        if ( outcome.FoundSceneVersion == 0 && outcome.FoundUnitVersion == 0 )
        {
            prefab.SceneVersion = Core::kSceneVersion;
            prefab.UnitVersion  = Core::kUnitVersion;
            outcome.Changed     = true;
            return outcome;
        }

        // Any OTHER pair was stamped by a build this tool does not know — most likely a newer engine.
        // Guessing here is the silent substitution the gate exists to forbid, so the file is refused
        // with the numbers named.
        outcome.Error = fmt::format(
             "stated scene schema v{0} / world units v{1}, and this tool only knows the step from an "
             "unversioned prefab (v0/v0) to v{2}/v{3}. A prefab stamped at another generation was written "
             "by a build this tool predates - convert it with THAT build's PrefabMigrator.",
             outcome.FoundSceneVersion, outcome.FoundUnitVersion, Core::kSceneVersion, Core::kUnitVersion );
        return outcome;
    }

} // namespace Desert::Migration
