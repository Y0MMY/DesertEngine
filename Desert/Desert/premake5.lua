project "Desert"
    kind "StaticLib"

    pchheader "pch.hpp"
    pchsource "Source/pch.cpp"
    forceincludes { "pch.hpp" }

    files { 
        "Source/pch.cpp",
        "Source/pch.hpp",
        "Source/Engine/**.cpp", 
        "Source/Engine/**.hpp",
        "%{wks.location}/ThirdParty/VulkanAllocator/vk_mem_alloc.cpp",
        "%{wks.location}/ThirdParty/stb/stb_image.cpp",
    }

    includedirs {
        "Source/",
        "%{wks.location}/Desert/Common/Source",
    }
    
    for name, path in pairs(deps.Common.IncludeDir) do
        includedirs { path }
    end
    
    for name, path in pairs(deps.DesertSpecific.IncludeDir) do
        includedirs { path }
    end

    links { 
        "Common",
        "ImGui",
        deps.Common.Libraries.yaml_cpp
    }
    
    for _, define in ipairs(deps.Common.Defines) do
        defines { define }
    end

    filter "configurations:Debug"
        for name, path in pairs(deps.DesertSpecific.Libraries.Debug) do
            links { path }
        end

    filter "configurations:Release"
        for name, path in pairs(deps.DesertSpecific.Libraries.Release) do
            links { path }
        end

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