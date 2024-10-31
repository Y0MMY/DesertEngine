
project "Common"
    kind "StaticLib"

    files { 
        -- COMMON 
        "Source/Common/**.cpp", 
        "Source/Common/**.hpp",
    }

    includedirs {
        "Source/",
        "Source/Common",
    }

    includedirs {
        "%{wks.location}/ThirdParty/spdlog/include/",
        "%{wks.location}/ThirdParty/yaml-cpp/include",
        "%{wks.location}/ThirdParty/glm/",
        "%{wks.location}/ThirdParty/GLFW/include",
    }
    
    links{
        "yaml-cpp",
    }

    defines { "YAML_CPP_STATIC_DEFINE" }
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
