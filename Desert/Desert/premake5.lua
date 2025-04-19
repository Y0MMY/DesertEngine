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
         -- stb
        "%{wks.location}/ThirdParty/stb/stb_image.cpp",
    }

    includedirs {
        "%{wks.location}/Desert/Desert/Source/",
        "%{wks.location}/Desert/Common/Source",
        
        "%{wks.location}/ThirdParty/spdlog/include/",
        "%{wks.location}/ThirdParty/yaml-cpp/include",
        "%{wks.location}/ThirdParty/glm/",
        "%{wks.location}/ThirdParty/GLFW/include",
        "%{wks.location}/ThirdParty/VulkanSDK/",
        "%{wks.location}/ThirdParty/stb/include/",
        "%{wks.location}/ThirdParty/assimp/include",

        "%{wks.location}/ThirdParty/VulkanSDK/shaderc/Include",
        "%{wks.location}/ThirdParty/VulkanSDK/spirv_cross/Include",
        "%{wks.location}/ThirdParty/VulkanSDK/include",

        "%{wks.location}/ThirdParty/",
    }

    local allLinks = {
        "Common",
        "ImGui",
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