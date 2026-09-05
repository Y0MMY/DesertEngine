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

    -- MSVC compiles the files of ONE project serially unless told otherwise, and `msbuild -m` — which CI
    -- already passes — parallelises PROJECTS, not the files inside them. So the two projects that hold
    -- almost all of the code, Desert and Editor, were each built on a single core while the rest of the
    -- runner idled.
    --
    -- Measured on run 33958917578, the step breakdown of Windows Debug: Build 57.9 min, Run tests
    -- 32.9 min, everything else 2.7 min combined. The build is the larger half and it is the half that
    -- was single-threaded. That job also runs at 93 minutes against a ceiling that had already cancelled
    -- it twice (see the note beside `timeout-minutes` in .github/workflows/ci.yml).
    --
    -- No effect on the makefile generators, which already get their parallelism from `make -j`.
    flags { "MultiProcessorCompile" }

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
