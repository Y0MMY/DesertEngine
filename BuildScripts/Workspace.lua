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
