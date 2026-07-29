project "Desert"
    kind "StaticLib"

    pchheader "pch.hpp"
    pchsource "Source/pch.cpp"
    forceincludes { "pch.hpp" }

    -- Reflection codegen (UHT-style): run DesertHeaderTool before compiling so
    -- Source/Engine/Generated/Reflection.gen.cpp is regenerated from REFLECT()/PROPERTY()
    -- annotations. The generated file is picked up by the Source/Engine/**.cpp glob below.
    dependson { "DesertHeaderTool" }
    prebuildcommands {
        DesertPlatform.BuiltToolPath("DesertHeaderTool")
            .. ' "' .. _MAIN_SCRIPT_DIR .. '/Desert/Desert/Source"'
            .. ' "' .. _MAIN_SCRIPT_DIR .. '/Desert/Desert/Source/Engine/Generated/Reflection.gen.cpp"'
            .. ' "Engine"'
    }

    files { 
        "Source/pch.cpp",
        "Source/pch.hpp",
        "Source/Engine/**.cpp", 
        "Source/Engine/**.hpp",
        "%{wks.location}/ThirdParty/VulkanAllocator/vk_mem_alloc.cpp",
        "%{wks.location}/ThirdParty/stb/stb_image.cpp",
        "%{wks.location}/ThirdParty/miniaudio/miniaudio.cpp",
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
        "Jolt",
        "Lua",
        "Optick",
        "MeshOptimizer",
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

    filter { "system:macosx" }
        defines { "DESERT_PLATFORM_MACOS" }
        files {
            "Source/Platform/MacOS/**.cpp",
            "Source/Platform/MacOS/**.hpp",
            "Source/Platform/MacOS/**.mm",
        }

    filter {}