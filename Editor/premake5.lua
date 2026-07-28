deps = include('Dependencies.lua')
-- dofile, not include: include() runs a file once and returns nil on repeat,
-- and Desert/Dependencies.lua was already included by the engine projects.
local engineDeps = dofile(_MAIN_SCRIPT_DIR .. '/Desert/Dependencies.lua')

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
        "%{wks.location}/ThirdParty/optick/src/",
        "%{wks.location}/ThirdParty/",
    }

    for name, path in pairs(deps.EditorSpecific.IncludeDir) do
        includedirs { path }
    end

    -- NOTE: no INCLUDE_HEADERS=#include<...> define here — nothing uses it, and
    -- the '#' turns the rest of the DEFINES line into a comment in gmake makefiles.
    defines { "YAML_CPP_STATIC_DEFINE",
              "USE_OPTICK=1", "OPTICK_ENABLE_GPU=0", "OPTICK_ENABLE_TRACING=0" }

    links{
        "Desert",
        "yaml-cpp",
        "GLFW",
        "Optick",
        "ImGuiNodeEditor",
    }

    filter "configurations:Debug"
    for name, path in pairs(deps.EditorSpecific.Libraries.Debug) do
        links { path }
    end

    filter "configurations:Release"
    for name, path in pairs(deps.EditorSpecific.Libraries.Release) do
        links { path }
    end

    filter "configurations:Debug"
        defines { "DESERT_CONFIG_DEBUG" }
        symbols "On"

    filter "configurations:Release"
        defines { "DESERT_CONFIG_RELEASE" }

    filter "system:windows"
        defines { "DESERT_PLATFORM_WINDOWS" }

    filter "system:macosx"
        defines { "DESERT_PLATFORM_MACOS" }

        -- Unlike Visual Studio, gmake does not link static-lib dependencies
        -- transitively — the executable has to pull in everything the engine
        -- libraries use, plus the Apple frameworks GLFW/MoltenVK rely on.
        links {
            "Common",
            "ImGui",
            "Jolt",
            "Lua",
            "ReflectCpp",
            "Cocoa.framework",
            "IOKit.framework",
            "CoreFoundation.framework",
            "CoreVideo.framework",
            "QuartzCore.framework",
        }

    -- Vulkan/shaderc/spirv-cross and reflect-cpp come from the engine's
    -- dependency list so the two stay in sync.
    filter { "system:macosx", "configurations:Debug" }
        for name, path in pairs(engineDeps.DesertSpecific.Libraries.Debug) do
            links { path }
        end

    filter { "system:macosx", "configurations:Release" }
        for name, path in pairs(engineDeps.DesertSpecific.Libraries.Release) do
            links { path }
        end

    filter {}
