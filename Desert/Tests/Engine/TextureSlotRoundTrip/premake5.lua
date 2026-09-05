local test_name = path.getname(_SCRIPT_DIR)
local test_files = os.matchfiles("*.cpp")

project(test_name)
    kind "ConsoleApp"
    language "C++"

    targetdir ("%{wks.location}/build/Bin/Tests/%{cfg.buildcfg}")
    objdir ("%{wks.location}/build/Tests/Intermediates/%{cfg.buildcfg}")

    -- The unit under test is the pair of functions that turn a texture handle into the string a `.desce`
    -- carries and back. They were extracted OUT of ComponentRegistry.cpp precisely so this suite could
    -- exist: that file reaches the ResourceRegistry and through it the whole renderer, so the two
    -- branches were unreachable by every suite in the repository for as long as they lived there.
    --
    -- The engine sources are listed rather than linked because libDesert pulls in Vulkan, and resolving a
    -- texture reference needs the AssetManager and the `.tex` parser and nothing else.
    files {
        test_files,
        "%{wks.location}/Desert/Desert/Source/Engine/Core/Serialize/TextureSlot.cpp",
        "%{wks.location}/Desert/Desert/Source/Engine/Assets/TextureAsset.cpp",
    }

    includedirs {
        "%{wks.location}/Desert/Common/Source",
        "%{wks.location}/Desert/Desert/Source",
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

    -- Common: the VFS the `.tex` is read through, the filesystem helper, the logger, UUID.
    -- Optick: Common's JobSystem registers its worker threads with the profiler.
    links { "Common", "Optick" }

    -- Common contains Objective-C (MacOSFileSystem's file dialog) and the loader reaches
    -- Common::Utils::FileSystem, so the ObjC runtime + AppKit have to link as well.
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
