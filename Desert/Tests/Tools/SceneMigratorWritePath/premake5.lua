local test_name = path.getname(_SCRIPT_DIR)
local test_files = os.matchfiles("*.cpp")

project(test_name)
    kind "ConsoleApp"
    language "C++"

    targetdir ("%{wks.location}/build/Bin/Tests/%{cfg.buildcfg}")
    objdir ("%{wks.location}/build/Tests/Intermediates/%{cfg.buildcfg}")

    -- Unlike the suites beside it, this one compiles the TOOL and not only the migrations: the claim
    -- under test is the file loop itself (read, write back atomically, exit code), which lives in
    -- MigratorMain.cpp behind RunSceneMigrator. SceneMigration.cpp comes along because the loop
    -- calls MigrateScene on every file it parses.
    files {
        test_files,
        "%{wks.location}/Tools/SceneMigrator/Source/MigratorMain.cpp",
        "%{wks.location}/Tools/SceneMigrator/Source/SceneMigration.cpp",
    }

    includedirs {
        "%{wks.location}/Desert/Common/Source",
        "%{wks.location}/Desert/Desert/Source",
        -- The migrations and the tool's loop live in the TOOL (see SceneMigratorEndToEnd's premake
        -- for why they moved out of the engine). This resolves <MigratorMain.hpp> and
        -- <SceneMigration.hpp>, and their own includes of themselves.
        "%{wks.location}/Tools/SceneMigrator/Source",
        "%{wks.location}/ThirdParty/entt/include/",       -- Components.hpp is an entt registry away
        "%{wks.location}/ThirdParty/reflect-cpp/include", -- the scene tree is rfl::Generic
    }

    for name, path in pairs(deps.Common.IncludeDir) do
        includedirs { path }
    end

    for name, path in pairs(deps.TestSpecific.IncludeDir) do
        includedirs { path }
    end

    for _, define in ipairs(deps.TestSpecific.Defines) do
        defines { define }
    end

    -- Components.hpp reaches engine headers that use DESERT_DEBUG_BREAK, which needs the platform.
    filter "system:windows"
        defines { "DESERT_PLATFORM_WINDOWS" }
    filter "system:macosx"
        defines { "DESERT_PLATFORM_MACOS" }
    filter "system:linux"
        defines { "DESERT_PLATFORM_LINUX" }
    filter {}

    -- Common: UUID/AssetHandle/the logger + the atomic write primitive the tool's loop goes through.
    -- Optick: Common's JobSystem registers its worker threads with the profiler.
    links { "Common", "Optick" }

    -- Common contains Objective-C (MacOSFileSystem file dialog) — referencing FileSystem pulls it in,
    -- so the ObjC runtime + AppKit must link too.
    filter "system:macosx"
        links { "Cocoa.framework", "Foundation.framework" }
    filter {}

    filter "system:not windows"
        links { "ReflectCpp" }
    filter {}

    filter "configurations:Debug"
        for name, path in pairs(deps.TestSpecific.Libraries.Debug) do
            links { path }
        end

    filter "configurations:Release"
        for name, path in pairs(deps.TestSpecific.Libraries.Release) do
            links { path }
        end

    filter {}

print("Configured test project: " .. test_name)
