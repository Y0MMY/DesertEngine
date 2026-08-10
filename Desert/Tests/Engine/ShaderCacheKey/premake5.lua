-- The SPIR-V cache key, the include closure it is computed over, and the descriptor count a pipeline
-- layout and a bound descriptor set have to agree on.
--
-- All three run with NO VkDevice, which is the only reason they are checkable on this machine:
-- ShaderCacheKey.cpp is pure file I/O + hashing, and VulkanShaderReflection.cpp holds no device (same
-- recipe as Tests/Engine/ShaderReflection). The engine's own translation units are compiled straight
-- into the test, so what is asserted is the code the runtime executes rather than a copy of it.
local deps = dofile(_MAIN_SCRIPT_DIR .. '/Desert/Dependencies.lua')

local test_name = path.getname(_SCRIPT_DIR)
local test_files = os.matchfiles("*.cpp")

project(test_name)
    kind "ConsoleApp"
    language "C++"

    targetdir ("%{wks.location}/build/Bin/Tests/%{cfg.buildcfg}")
    objdir ("%{wks.location}/build/Tests/Intermediates/%{cfg.buildcfg}")

    files {
        test_files,
        "%{wks.location}/Desert/Desert/Source/Engine/Core/ShaderCompiler/ShaderCacheKey.cpp",
        "%{wks.location}/Desert/Desert/Source/Engine/Core/ShaderCompiler/DShader/DShaderParser.cpp",
        "%{wks.location}/Desert/Desert/Source/Engine/Graphic/API/Vulkan/VulkanShaderReflection.cpp",
    }

    includedirs {
        "%{wks.location}/Desert/Common/Source",
        "%{wks.location}/Desert/Desert/Source",
    }

    for name, path in pairs(deps.Common.IncludeDir) do
        includedirs { path }
    end

    -- Vulkan headers (for the descriptor types), shaderc and spirv-cross.
    for name, path in pairs(deps.DesertSpecific.IncludeDir) do
        includedirs { path }
    end

    for name, path in pairs(deps.TestSpecific.IncludeDir) do
        includedirs { path }
    end

    for _, define in ipairs(deps.TestSpecific.Defines) do
        defines { define }
    end

    links { "Common", "Optick" } -- Common's JobSystem registers worker threads with Optick

    -- Common contains Objective-C (MacOSFileSystem's file dialog) and the include walk goes through
    -- Common::Utils::FileSystem, so the ObjC runtime + AppKit have to link as well.
    filter "system:macosx"
        links { "Cocoa.framework", "Foundation.framework" }
    filter {}

    filter "configurations:Debug"
        for name, path in pairs(deps.TestSpecific.Libraries.Debug) do
            links { path }
        end
        for name, path in pairs(deps.DesertSpecific.Libraries.Debug) do
            links { path }
        end

    filter "configurations:Release"
        for name, path in pairs(deps.TestSpecific.Libraries.Release) do
            links { path }
        end
        for name, path in pairs(deps.DesertSpecific.Libraries.Release) do
            links { path }
        end

    filter {}

print("Configured test project: " .. test_name)
