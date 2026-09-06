-- PrefabMigrator — the only thing in this repository that knows an old .deprefab format.
--
-- The engine's prefab loader refuses an unversioned (or foreign-generation) prefab and names this tool
-- (Engine/Assets/Prefab/PrefabFormat.cpp). Like SceneMigrator it reaches into the engine for HEADERS —
-- the current on-disk struct and the two generation integers — and additionally COMPILES the engine's
-- one small gate TU (PrefabFormat.cpp), so the check this tool applies to what it writes is the same
-- function the loader applies to what it reads, not a second statement of it.
-- dofile, not include: the dependency list was already include()'d by the engine projects.
local deps = dofile(_MAIN_SCRIPT_DIR .. '/Desert/Dependencies.lua')

project "PrefabMigrator"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"

    files {
        "Source/**.cpp",
        "%{wks.location}/Desert/Desert/Source/Engine/Assets/Prefab/PrefabFormat.cpp",
    }

    includedirs {
        "Source",
        "%{wks.location}/Desert/Common/Source",
        "%{wks.location}/Desert/Desert/Source",
    }
    externalincludedirs {
        "%{wks.location}/ThirdParty/entt/include",         -- PrefabData reaches ECS headers
        "%{wks.location}/ThirdParty/reflect-cpp/include",  -- the prefab tree is rfl-serialized
    }

    for name, path in pairs(deps.Common.IncludeDir) do
        externalincludedirs { path }
    end

    -- Common: UUID/AssetHandle. Optick: Common's JobSystem registers its workers with the profiler.
    links { "Common", "Optick", "ReflectCpp" }

    filter "configurations:Debug"
        symbols "On"

    filter "configurations:Release"
        optimize "On"

    filter "system:windows"
        defines { "DESERT_PLATFORM_WINDOWS" }

    filter "system:macosx"
        defines { "DESERT_PLATFORM_MACOS" }
        -- Common contains Objective-C (file dialog); linking it needs AppKit + the ObjC runtime.
        links { "Cocoa.framework", "Foundation.framework" }

    filter "system:linux"
        defines { "DESERT_PLATFORM_LINUX" }

    filter {}
