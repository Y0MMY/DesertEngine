local test_name = path.getname(_SCRIPT_DIR)
local test_files = os.matchfiles("*.cpp")

project(test_name)
    kind "ConsoleApp"
    language "C++"

    targetdir ("%{wks.location}/build/Bin/Tests/%{cfg.buildcfg}")
    objdir ("%{wks.location}/build/Tests/Intermediates/%{cfg.buildcfg}")

    -- The migration and the engine gate it must agree with, and nothing else. The migration is a pure
    -- function over the parsed tree and the gate is a pure function over the same tree; this project
    -- linking without a renderer, an asset manager or a scene is the proof, exactly as it is for the
    -- scene migration suites beside this one.
    files {
        test_files,
        "%{wks.location}/Tools/PrefabMigrator/Source/PrefabMigration.cpp",
        "%{wks.location}/Desert/Desert/Source/Engine/Assets/Prefab/PrefabFormat.cpp",
    }

    includedirs {
        "%{wks.location}/Desert/Common/Source",
        "%{wks.location}/Desert/Desert/Source",
        -- The migration lives in the TOOL (the engine loader only refuses; see PrefabFormat.hpp). This
        -- is what makes `#include <PrefabMigration.hpp>` resolve.
        "%{wks.location}/Tools/PrefabMigrator/Source",
        "%{wks.location}/ThirdParty/entt/include/",       -- PrefabData reaches ECS headers
        "%{wks.location}/ThirdParty/reflect-cpp/include", -- the prefab tree is rfl-serialized
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

    -- PrefabData.hpp reaches engine headers that use DESERT_DEBUG_BREAK, which needs the platform.
    filter "system:windows"
        defines { "DESERT_PLATFORM_WINDOWS" }
    filter "system:macosx"
        defines { "DESERT_PLATFORM_MACOS" }
    filter "system:linux"
        defines { "DESERT_PLATFORM_LINUX" }
    filter {}

    -- Common: UUID and AssetHandle. Optick: Common's JobSystem registers its worker threads with it.
    links { "Common", "Optick" }

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
