-- The per-frame scene payload of the PBR materials, and the relation between the skinned mesh path and
-- the static one: one snapshot type, one applier, one set of binding NAMES that all three mesh PBR
-- shaders declare.
--
-- No VkDevice anywhere: the material headers are used for their types and constants only (sizeof,
-- std::is_same, the binding-name constants), and the shaders are compiled with shaderc and reflected by
-- VulkanShaderReflection.cpp, which holds no device — the same recipe as Tests/Engine/ShaderCacheKey.
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

    -- The platform macro, the same three lines every other engine-linking test carries. DESERT_DEBUG_BREAK
    -- falls back to MSVC's __debugbreak() when none of the three is defined, so ANY engine header that
    -- reaches DESERT_VERIFY fails to compile here without it.
    filter "system:windows"
        defines { "DESERT_PLATFORM_WINDOWS" }
    filter "system:macosx"
        defines { "DESERT_PLATFORM_MACOS" }
    filter "system:linux"
        defines { "DESERT_PLATFORM_LINUX" }
    filter {}

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
