local test_name = path.getname(_SCRIPT_DIR)
local test_files = os.matchfiles("*.cpp")

project(test_name)
    kind "ConsoleApp"
    language "C++"

    targetdir ("%{wks.location}/build/Bin/Tests/%{cfg.buildcfg}")
    objdir ("%{wks.location}/build/Tests/Intermediates/%{cfg.buildcfg}")

    -- The REAL packer and the real project context, not mirrors: the suite calls BuildContentPak()
    -- over a temp project and then plays the packaged game's side of the mount. Both files are
    -- CPU + filesystem only — nothing from the renderer.
    files {
        test_files,
        "%{wks.location}/Editor/Source/Editor/Packaging/GamePackager.cpp",
        "%{wks.location}/Desert/Desert/Source/Engine/Project/ProjectContext.cpp",
    }

    includedirs {
        "%{wks.location}/Desert/Common/Source",
        "%{wks.location}/Desert/Desert/Source",
        "%{wks.location}/Editor/Source",
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

    links { "Common", "Optick" } -- Commons JobSystem registers worker threads with Optick

    filter "system:not windows"
        links { "ReflectCpp" }

    -- Common contains Objective-C (MacOSFileSystem file dialog) — pulled in because this test
    -- references FileSystem, so the ObjC runtime + AppKit must link too.
    filter "system:macosx"
        links { "Cocoa.framework", "Foundation.framework" }
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
