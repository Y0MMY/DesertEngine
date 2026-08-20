local test_name = path.getname(_SCRIPT_DIR)
local test_files = os.matchfiles("*.cpp")

project(test_name)
    kind "ConsoleApp"
    language "C++"

    targetdir ("%{wks.location}/build/Bin/Tests/%{cfg.buildcfg}")
    objdir ("%{wks.location}/build/Tests/Intermediates/%{cfg.buildcfg}")

    -- TWO units under test, and the point of the suite is the relation BETWEEN them, GPU-free:
    --   * Editor/Resources/Shaders/Common/CloudShadowMap.glslh — the triple's encode, its reconstruction
    --     and the march that fills it — compiled AS C++ through CloudShadowReference.hpp, together with
    --     Common/CloudGeometry.glslh for the shell the ray crosses. That is why the SHADER ROOT is on the
    --     include path: the test drives the exact text the two cloud passes compile.
    --   * Engine/Graphic/Clouds/CloudShadowPayload.hpp — the C++ mirror of those constants and the
    --     orthographic projection with its two snaps. Header-only and pure, so nothing is linked from the
    --     engine for it.
    -- Nothing to link — no renderer, no Vulkan.
    files {
        test_files,
    }

    includedirs {
        "%{wks.location}/Desert/Common/Source",
        "%{wks.location}/Desert/Desert/Source",
        "%{wks.location}/Editor/Resources/Shaders",
        -- CloudShadowPayload.hpp -> CloudPayload.hpp -> VolumetricCloudComponent.hpp, which reaches the
        -- reflection macros and, through Assets/Common.hpp, an entt registry.
        "%{wks.location}/ThirdParty/entt/include/",
        "%{wks.location}/ThirdParty/reflect-cpp/include",
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
