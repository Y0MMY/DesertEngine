-- SceneMigrator — the ONLY thing in this repository that knows an old .desce format.
--
-- The migrations used to live in the engine (Engine/Core/Serialize/SceneMigration.cpp) and run on every
-- scene load; they are Source/SceneMigration.cpp here now, and the engine loader REFUSES an old file and
-- names this tool instead. Nothing about the functions changed — they were always pure functions over the
-- parsed tree, needing no GPU, no asset manager and no scene graph, which is exactly what let them move.
--
-- It still reaches into the engine for HEADERS and not for code: the current on-disk struct
-- (Engine/Core/Serialize/SceneFormat.hpp), the tonemap enum and the shipped cloud-type names. Those are
-- statements of the CURRENT format, and a second copy of any of them here is a format that can fork.
-- dofile, not include: the dependency list was already include()'d by the engine projects.
local deps = dofile(_MAIN_SCRIPT_DIR .. '/Desert/Dependencies.lua')

project "SceneMigrator"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"

    files {
        "Source/**.cpp",
        -- THE ENGINE'S OWN REFLECTION TABLE, not a copy of it. The v13 -> v14 step canonicalises the
        -- Settings block, and "canonical" means "the bytes the engine's saver would write" — so it has
        -- to enumerate the same 51 fields in the same order through the same serializer. A hand-written
        -- field list here would be a second statement of the format, which is the fork this tool's own
        -- header forbids.
        --
        -- It costs nothing but compile time: these three compile against Common alone, with no GPU, no
        -- window and no Desert link (Desert/Tests/Engine/ConfigOwnership and SceneForeignKeys build on
        -- exactly this recipe). Reflection.gen.cpp is emitted by DesertHeaderTool as a PREBUILD STEP OF
        -- `Desert`, hence the dependency below.
        "%{wks.location}/Desert/Desert/Source/Engine/Reflection/ReflectionSerializer.cpp",
        "%{wks.location}/Desert/Desert/Source/Engine/Reflection/ReflectionRegistry.cpp",
        "%{wks.location}/Desert/Desert/Source/Engine/Generated/Reflection.gen.cpp",
    }

    dependson { "Desert" }

    includedirs {
        "%{wks.location}/Desert/Common/Source",
        "%{wks.location}/Desert/Desert/Source",
    }
    externalincludedirs {
        "%{wks.location}/ThirdParty/entt/include",         -- PrefabData reaches ECS headers
        "%{wks.location}/ThirdParty/reflect-cpp/include",  -- the scene tree is rfl::Generic
    }

    for name, path in pairs(deps.Common.IncludeDir) do
        externalincludedirs { path }
    end

    -- Common: UUID/AssetHandle/the logger the rejection warnings go through.
    -- Optick: Common's JobSystem registers its worker threads with the profiler.
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
