project "Common"
    kind "StaticLib"

    files { 
        "Source/Common/**.cpp", 
        "Source/Common/**.hpp",
    }

    includedirs {
        "Source/",
        "Source/Common",
    }
    
    for name, path in pairs(deps.Common.IncludeDir) do
        includedirs { path }
    end
    
    for name, path in pairs(deps.CommonSpecific.IncludeDir) do
        includedirs { path }
    end
    
    links { deps.Common.Libraries.yaml_cpp }
    
    for _, define in ipairs(deps.Common.Defines) do
        defines { define }
    end
    
    for _, define in ipairs(deps.CommonSpecific.Defines) do
        defines { define }
    end

    filter "configurations:Debug"
        defines { "DESERT_CONFIG_DEBUG" }
        symbols "On"

    filter "configurations:Release"
        defines { "DESERT_CONFIG_RELEASE" }

    filter { "system:windows" }
        defines { "DESERT_PLATFORM_WINDOWS" }
        files {
            "Source/Common/Platform/Windows/**.cpp",
            "Source/Common/Platform/Windows/**.hpp",
        }