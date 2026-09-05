-- Workspace-wide settings shared by every project.
-- The workspace itself is declared here; per-configuration and per-platform
-- blocks live in Configurations.lua / PlatformWindows.lua / PlatformMacOS.lua.

workspace "Desert"
    -- The workspace is declared here but its files (Desert.sln / Makefile)
    -- belong at the repo root, same as before the config split.
    location ( _MAIN_SCRIPT_DIR )

    configurations { "Debug", "Release" }
    startproject "Editor"

    language "C++"
    cppdialect "C++20"
    -- Anchored to the repo root: relative paths here would resolve against
    -- BuildScripts/ (the directory of this script), not the workspace.
    targetdir ( _MAIN_SCRIPT_DIR .. "/build/Bin/%{cfg.buildcfg}" )
    objdir ( _MAIN_SCRIPT_DIR .. "/build/Intermediates/%{cfg.buildcfg}" )

    externalanglebrackets "On"
    externalwarnings "Off"
    warnings "Off"

    -- NO `MultiProcessorCompile` HERE, AND THE ABSENCE IS THE MEASUREMENT.
    --
    -- It was added on 2026-09-05 on a reading that looked airtight: `msbuild -m`, which CI passes,
    -- parallelises PROJECTS and not the files inside them, so Desert and Editor — which hold almost all
    -- of the code — were each compiled on one core. The step breakdown of Windows Debug backed it up
    -- (run 33958917578: Build 57.9 min, Run tests 32.9 min, everything else 2.7 min).
    --
    -- Then the runs came back and said the opposite. Windows Debug BEFORE the flag: 73, 73, 76, 84 min.
    -- AFTER it: 93.6, 94.4, 92.3. Roughly twenty minutes SLOWER.
    --
    -- The likely mechanism is the one this project spent the same day learning on its own hardware:
    -- `-m` and `/MP` MULTIPLY. Up to four project builds, each spawning up to four compilers, on a
    -- four-core hosted runner — sixteen processes for four cores, which is oversubscription, not
    -- parallelism.
    --
    -- The honest caveat: seven merges landed the same day and the suite count went 95 -> 99, so part of
    -- that twenty minutes is the tree growing. Two variables moved at once, which is exactly what this
    -- project's own rule forbids. So the flag is reverted to restore the known-good baseline, and the
    -- next Windows run answers the question with ONE variable changed: back near 73-84 means the flag
    -- was the cost; still near 93 means the tree grew and the flag was innocent.
    --
    -- THE ANSWER CAME BACK, AND IT ACQUITS THE FLAG. Run 33976407162, the revert, one variable moved:
    -- Windows Debug **93 min WITHOUT it** — indistinguishable from the 92-94 measured WITH it, and
    -- twenty minutes above the 73-84 baseline that predates both. So the twenty minutes is the TREE
    -- GROWING, not `/MP`, and the `-m` x `/MP` oversubscription story above is a plausible mechanism
    -- that this project never actually observed. It is written down because it was the reasoning at the
    -- time, not because it was confirmed.
    --
    -- What that leaves: the flag was never fairly measured in either direction. Re-adding it is now a
    -- legitimate experiment for task И1 rather than a mistake to avoid — one variable, one run, and if
    -- it is kept, drop `-m` from the msbuild invocation in the SAME change so the two cannot multiply.
    -- The ceiling is not the pressure it was: 93 min against `timeout-minutes: 150` is 62%, where the
    -- job had previously been cancelled twice at 90. The pressure is that the tree keeps growing.

    -- EnTT hands each component type a sequential index from ONE global counter, and without this the
    -- counter is a plain `id_type` incremented with `value++` (ENTT_MAYBE_ATOMIC, entt.hpp). Two threads
    -- first-touching two different component types can then be handed the SAME index and read each
    -- other's storage. Scene::PrepareComponentPools creates every pool serially so the engine's own
    -- collectors never reach that counter concurrently; this define is what makes the counter safe for
    -- every OTHER first touch (preview scenes, thumbnail scenes, scripts) and costs one atomic increment
    -- per component type per process.
    --
    -- WORKSPACE SCOPE IS THE POINT, not tidiness: the macro changes the TYPE of a shared static, so a
    -- project that compiled entt without it would disagree about that object's layout with every project
    -- that did. It must be all of them or none.
    defines { "ENTT_USE_ATOMIC" }
