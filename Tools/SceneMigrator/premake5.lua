-- SceneMigrator — standalone CLI over the ENGINE's own scene migrations (Engine/Core/Serialize/
-- SceneMigration.cpp). It compiles that one translation unit directly instead of linking the engine,
-- the same recipe DShaderTool uses: the migrations are pure functions over the
-- parsed tree, so they need no GPU, no asset manager and no scene graph — and the files this tool
-- writes can never be produced by different code from the one the loader runs.
-- dofile, not include: the dependency list was already include()'d by the engine projects.
local deps = dofile(_MAIN_SCRIPT_DIR .. '/Desert/Dependencies.lua')

project "SceneMigrator"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"

    files {
        "Source/**.cpp",
        "%{wks.location}/Desert/Desert/Source/Engine/Core/Serialize/SceneMigration.cpp",
    }

    includedirs {
        "%{wks.location}/Desert/Common/Source",
        "%{wks.location}/Desert/Desert/Source",
        "%{wks.location}/ThirdParty/entt/include",         -- PrefabData reaches ECS headers
        "%{wks.location}/ThirdParty/reflect-cpp/include",  -- the scene tree is rfl::Generic
    }

    for name, path in pairs(deps.Common.IncludeDir) do
        includedirs { path }
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
