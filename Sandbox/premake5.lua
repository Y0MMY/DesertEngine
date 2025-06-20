project "Sandbox"
    kind "ConsoleApp"

    files { 
        -- Engine 
        "Source/**.cpp", 
        "Source/**.hpp",
    }

    includedirs {
        "%{wks.location}/Desert/Desert/Source/",
        "%{wks.location}/Sandbox/Source/",

        "%{wks.location}/Desert/Common/Source/",

        "%{wks.location}/ThirdParty/spdlog/include/",
        "%{wks.location}/ThirdParty/GLFW/include/",
        "%{wks.location}/ThirdParty/Glad/include/",
        "%{wks.location}/ThirdParty/entt/include/",
        "%{wks.location}/ThirdParty/glm/",
        "%{wks.location}/ThirdParty/assimp/include",
        "%{wks.location}/ThirdParty/",
    }

    defines { "INCLUDE_HEADERS=#include <Engine/Desert.hpp>","YAML_CPP_STATIC_DEFINE" }

    links{
        "Desert",
        "yaml-cpp",
        "GLFW",
    }

    filter "configurations:Debug"
        defines { "DESERT_CONFIG_DEBUG" }
        symbols "On"

    filter "configurations:Release"
        defines { "DESERT_CONFIG_RELEASE" }
