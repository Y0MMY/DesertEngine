local currentDir = _MAIN_SCRIPT_DIR
local dependenciesPath = currentDir .. "/Dependencies.lua"

local dependencies = include(dependenciesPath)

project "Desert"
    kind "StaticLib"

    pchheader "pch.hpp"
    pchsource "Source/pch.cpp"
    forceincludes { "pch.hpp" }

    files { 
        -- Precompiled header
        "Source/pch.cpp",
        "Source/pch.hpp",
        
        -- Engine 
        "Source/Engine/**.cpp", 
        "Source/Engine/**.hpp",

        "%{wks.location}/ThirdParty/VulkanSDK/VulkanAllocator/vk_mem_alloc.cpp",
    }

    includedirs {
        "%{wks.location}/Desert/Desert/Source/",
        "%{wks.location}/Desert/Common/Source",
        "%{wks.location}/ThirdParty/spdlog/include/",
        "%{wks.location}/ThirdParty/yaml-cpp/include",
        "%{wks.location}/ThirdParty/glm/",
        "%{wks.location}/ThirdParty/GLFW/include",
        "%{wks.location}/ThirdParty/VulkanSDK/",


        "%{wks.location}/ThirdParty/VulkanSDK/shaderc/Include",
        "%{wks.location}/ThirdParty/VulkanSDK/spirv_cross/Include",
        "%{wks.location}/ThirdParty/VulkanSDK/include",
    }

    -- includedirs {
    --     "../ThirdParty/spdlog/include/",
    --     "../ThirdParty/GLFW/include/",
    --     "../ThirdParty/glm/",
    --     "../ThirdParty/VulkanSDK/shaderc/Include",
    --     "../ThirdParty/VulkanSDK/spirv_cross/Include",
    --     "../ThirdParty/VulkanSDK/include",
    --     "../ThirdParty/Glad/include/",
    --     "../ThirdParty/stb/include/",
    --     "../ThirdParty/assimp/include",
    --     "../ThirdParty/yaml-cpp/include",
    --     "../ThirdParty/ImGUI/",
    --     "%{IncludeDir.entt}",
        
    --     "../ThirdParty/",
    -- }

    -- links
    -- {
    --     "ImGui",
    --     "yaml-cpp",
        
    --     "%{LibraryDir.shaderc_Debug}",
    --     "%{LibraryDir.spirv_cross_core_Debug}",
    --     "%{LibraryDir.spirv_cross_glsl_Debug}",
    --     "%{LibraryDir.shaderc_shared_Debug}",
    --     "%{LibraryDir.shaderc_combined_Debug}",
    --     "%{LibraryDir.shaderc_util_Debug}",
    --     "%{LibraryDir.vulkan1}",
    --     "%{LibraryDir.assimp_Debug}",
    --     "%{LibraryDir.OGLCompiler_Debug}",
    -- }
    local allLinks = {
        "Common",
    }

    for _, libPath in pairs(dependencies.LibraryDir) do
        table.insert(allLinks, libPath)
    end

    links(allLinks)

    defines { "YAML_CPP_STATIC_DEFINE" }
    filter "configurations:Debug"
        defines { "DESERT_CONFIG_DEBUG" }
        symbols "On"


    filter "configurations:Release"
        defines { "DESERT_CONFIG_RELEASE" }

    filter { "system:windows" }
        defines { "DESERT_PLATFORM_WINDOWS" }

        files {
            "Source/Platform/Windows/**.cpp",
            "Source/Platform/Windows/**.hpp",
        }