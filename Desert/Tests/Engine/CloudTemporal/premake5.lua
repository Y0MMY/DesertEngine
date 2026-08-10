local test_name = path.getname(_SCRIPT_DIR)
local test_files = os.matchfiles("*.cpp")

project(test_name)
    kind "ConsoleApp"
    language "C++"

    targetdir ("%{wks.location}/build/Bin/Tests/%{cfg.buildcfg}")
    objdir ("%{wks.location}/build/Tests/Intermediates/%{cfg.buildcfg}")

    -- Two units under test, both GPU-free:
    --   * Editor/Resources/Shaders/Common/CloudTemporal.glslh — the depth-guide packing, the bilateral
    --     upsample weights, the reprojection through the shell mid-surface, the neighbourhood clamp and
    --     the blend — compiled AS C++ through CloudTemporalReference.hpp. That is why the SHADER ROOT is
    --     on the include path: the test drives the exact text the resolve and the composite compile.
    --   * Engine/Graphic/Clouds/CloudPayload.hpp — the temporal push constant, the mode's one and only
    --     interpretation, and the per-renderer memory arithmetic. Header-only, so it links without the
    --     engine.
    -- Nothing to link — no renderer, no Vulkan.
    files {
        test_files,
    }

    includedirs {
        "%{wks.location}/Desert/Common/Source",
        "%{wks.location}/Desert/Desert/Source",
        "%{wks.location}/Editor/Resources/Shaders",
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

    filter "system:windows"
        defines { "DESERT_PLATFORM_WINDOWS" }
    filter "system:macosx"
        defines { "DESERT_PLATFORM_MACOS" }
    filter "system:linux"
        defines { "DESERT_PLATFORM_LINUX" }
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
