local test_name = path.getname(_SCRIPT_DIR)
local test_files = os.matchfiles("*.cpp")

project(test_name)
    kind "ConsoleApp"
    language "C++"

    targetdir ("%{wks.location}/build/Bin/Tests/%{cfg.buildcfg}")
    objdir ("%{wks.location}/build/Tests/Intermediates/%{cfg.buildcfg}")

    -- Three GPU-free headers under test, and they are the whole of the hero-cloud data path:
    --   * Engine/Graphic/Clouds/CloudVolumeFormat.hpp — the .dvol layout, the conservative signed-
    --     distance encoding, and the read/write round trip.
    --   * Engine/Graphic/Clouds/CloudVolumeBake.hpp — the primitives, the smooth union and the bake.
    --     The BAKER TOOL compiles this same header, so a passing test is a statement about the volumes
    --     that actually get shipped, not about a copy of the maths.
    --   * Engine/Graphic/Clouds/CloudVolumeAtlasLayout.hpp — the tile arithmetic, and the guard-band
    --     property that stops one hero cloud reading another's voxels.
    --   * Engine/Graphic/Clouds/CloudVolumeInstance.hpp — the world->local transform every hero cloud is
    --     sampled through, including the Y-up -> Z-up turn phase 1a flagged as the likeliest thing to
    --     get silently wrong.
    --   * Editor/Resources/Shaders/Common/CloudVolumeAtlas.glslh — the SHADER's own copy of the tile
    --     arithmetic, compiled AS C++ through CloudVolumeShaderReference.hpp and checked against the
    --     first bullet. That is why the SHADER ROOT is on the include path: the test drives the exact
    --     text the march compiles, not a CPU re-implementation of it.
    -- All are header-only, so nothing is linked — no renderer, no Vulkan.
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
