local test_name = path.getname(_SCRIPT_DIR)
local test_files = os.matchfiles("*.cpp")

project(test_name)
    kind "ConsoleApp"
    language "C++"

    targetdir ("%{wks.location}/build/Bin/Tests/%{cfg.buildcfg}")
    objdir ("%{wks.location}/build/Tests/Intermediates/%{cfg.buildcfg}")

    -- The units under test are header-only pure math: Engine/Graphic/SkyRules.hpp, SkyPayload.hpp,
    -- AtmosphereEnv.hpp, SkySettings.hpp. No renderer, no GPU — AtmosphereEnv only FORWARD-declares the
    -- storage buffer it carries a handle to, which is exactly what makes it testable.
    --
    -- ...and Clouds/CloudEnvironmentBake.hpp since Р15, because the panorama carries the clouds now and
    -- "when is the environment stale" stopped being a question about the sun alone. That header reaches
    -- CloudPayload.hpp and through it the cloud COMPONENT, whose AssetHandle members pull in Common::UUID
    -- and whose PROPERTY macros pull in reflect-cpp — hence the include path and the three links below.
    -- Restating the payload here instead would be a mirror of the exact struct the fingerprint hashes.
    files {
        test_files,
    }

    includedirs {
        "%{wks.location}/Desert/Common/Source",
        "%{wks.location}/Desert/Desert/Source",
        "%{wks.location}/ThirdParty/reflect-cpp/include", -- reached through the cloud component's headers
    }

    -- Common: Common::UUID, behind the AssetHandle members of the cloud component.
    -- Optick: Common's JobSystem registers its worker threads with the profiler.
    links { "Common", "Optick" }

    filter "system:not windows"
        links { "ReflectCpp" }
    filter {}

    for name, path in pairs(deps.Common.IncludeDir) do
        includedirs { path }
    end

    for name, path in pairs(deps.TestSpecific.IncludeDir) do
        includedirs { path }
    end

    for _, define in ipairs(deps.TestSpecific.Defines) do
        defines { define }
    end

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
