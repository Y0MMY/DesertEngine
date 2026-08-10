local test_name = path.getname(_SCRIPT_DIR)
local test_files = os.matchfiles("*.cpp")

project(test_name)
    kind "ConsoleApp"
    language "C++"

    targetdir ("%{wks.location}/build/Bin/Tests/%{cfg.buildcfg}")
    objdir ("%{wks.location}/build/Tests/Intermediates/%{cfg.buildcfg}")

    -- Reflection.gen.cpp is written by DesertHeaderTool, which runs as a PREBUILD STEP OF `Desert` — so
    -- without this the parallel build can compile the table for this test before the codegen has rewritten
    -- it, and the test then reports on a stale component set. Build-order only; nothing is linked from it.
    dependson { "Desert" }

    -- The GENERATED reflection table is the thing under test, so it is compiled straight into the test
    -- rather than linked from the engine: the registrations are static initializers in an anonymous
    -- namespace, and Reflection.gen.cpp + the registry are the only two translation units they need.
    files {
        test_files,
        "%{wks.location}/Desert/Desert/Source/Engine/Generated/Reflection.gen.cpp",
        "%{wks.location}/Desert/Desert/Source/Engine/Reflection/ReflectionRegistry.cpp",
    }

    includedirs {
        "%{wks.location}/Desert/Common/Source",
        "%{wks.location}/Desert/Desert/Source",
        "%{wks.location}/ThirdParty/entt/include/", -- Components.hpp is an entt registry away
        "%{wks.location}/ThirdParty/reflect-cpp/include", -- ReflectionTypes.hpp -> <rflcpp/rfl/Generic.hpp>
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

    -- Components.hpp reaches engine headers that use DESERT_DEBUG_BREAK, which needs to know the platform.
    filter "system:windows"
        defines { "DESERT_PLATFORM_WINDOWS" }
    filter "system:macosx"
        defines { "DESERT_PLATFORM_MACOS" }
    filter "system:linux"
        defines { "DESERT_PLATFORM_LINUX" }
    filter {}

    -- Common: the generated table default-constructs an AssetHandle, which is a Common::UUID.
    -- Optick: Common's JobSystem registers its worker threads with the profiler.
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
