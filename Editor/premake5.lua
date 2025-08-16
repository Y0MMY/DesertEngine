deps = include('Dependencies.lua')

project "Editor"
    kind "ConsoleApp"

    files { 
        -- Engine 
        "Source/**.cpp", 
        "Source/**.hpp",
        "ThirdParty/ImGuizmo/ImGuizmo.cpp",
    }

    includedirs {
        "%{wks.location}/Desert/Desert/Source/",
        "%{wks.location}/Editor/Source/",

        "%{wks.location}/Desert/Common/Source/",

        "%{wks.location}/ThirdParty/spdlog/include/",
        "%{wks.location}/ThirdParty/GLFW/include/",
        "%{wks.location}/ThirdParty/Glad/include/",
        "%{wks.location}/ThirdParty/entt/include/",
        "%{wks.location}/ThirdParty/ImGui/",
        "%{wks.location}/ThirdParty/glm/",
        "%{wks.location}/ThirdParty/assimp/include",
        "%{wks.location}/ThirdParty/",
    }

    for name, path in pairs(deps.Common.IncludeDir) do
        includedirs { path }
    end

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
