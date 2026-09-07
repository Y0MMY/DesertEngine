local test_name = path.getname(_SCRIPT_DIR)
local test_files = os.matchfiles("*.cpp")

project(test_name)
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"

    targetdir ("%{wks.location}/build/Bin/Tests/%{cfg.buildcfg}")
    objdir ("%{wks.location}/build/Tests/Intermediates/%{cfg.buildcfg}")

    -- The LAUNCHER's half of the two contracts it shares with the engine:
    --   * D-launch — the command it composes to start the Editor (LaunchProtocol.hpp says each side
    --     asserts its own end; before this suite the launcher's end was only a claim);
    --   * the project formats — what it writes into a project and reads back out of the registry.
    -- It compiles the hub's own non-UI sources directly, exactly as the standalone launcher
    -- repository will after L3 — no engine library, no window, no device: only the desert-shared
    -- serializer and gtest.
    files {
        test_files,
        "%{wks.location}/Tools/ProjectHub/Source/Files.cpp",
        "%{wks.location}/Tools/ProjectHub/Source/Launch.cpp",
        "%{wks.location}/Tools/ProjectHub/Source/Projects.cpp",
        "%{wks.location}/Tools/ProjectHub/Source/HubConfig.cpp",
        -- The thumbnail cache decodes PNGs and hands them to a backend it is GIVEN, so the suite can
        -- run it with a counting backend and no window, no device and no graphics API at all — which
        -- is the whole reason the decode budget is testable.
        "%{wks.location}/Tools/ProjectHub/Source/Thumbnails.cpp",
        "%{wks.location}/ThirdParty/stb/stb_image.cpp",
        "%{wks.location}/ThirdParty/desert-shared/Source/ProjectFormat.cpp",
        "%{wks.location}/ThirdParty/desert-shared/Source/EngineRegistry.cpp",
    }

    includedirs {
        "%{wks.location}/Tools/ProjectHub/Source",
        "%{wks.location}/ThirdParty/desert-shared/Include",
    }
    -- Third-party headers are EXTERNAL: -Wall -Wextra is on tree-wide now, and vendored headers
    -- would otherwise drown our own diagnostics in theirs.
    externalincludedirs {
        "%{wks.location}/ThirdParty/spdlog/include", -- the fmt headers ResultStr.hpp needs
        "%{wks.location}/ThirdParty/stb/include",
    }

    for name, path in pairs(deps.TestSpecific.IncludeDir) do
        externalincludedirs { path }
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
