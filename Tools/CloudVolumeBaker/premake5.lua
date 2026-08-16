-- CloudVolumeBaker — standalone CLI over the ENGINE's hero-cloud baker. Engine/Graphic/Clouds/
-- CloudVolumeBake.hpp is header-only and GPU-free, so the tool compiles it directly rather than
-- linking the engine (the same recipe as DShaderTool and its parser): the volumes CI bakes can never
-- be produced by different maths from the ones the renderer samples.
-- dofile, not include: the dependency list was already include()'d by the engine projects.
local deps = dofile(_MAIN_SCRIPT_DIR .. '/Desert/Dependencies.lua')

project "CloudVolumeBaker"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"

    files {
        "Source/**.cpp",
    }

    includedirs {
        "%{wks.location}/Desert/Common/Source",
        "%{wks.location}/Desert/Desert/Source",
        -- reflect-cpp reads the authored .cloudshape.json; stb writes the slice PNGs that make a bake
        -- reviewable without a GPU.
        "%{wks.location}/ThirdParty/reflect-cpp/include",
        "%{wks.location}/ThirdParty/stb/include",
    }

    for name, path in pairs(deps.Common.IncludeDir) do
        includedirs { path }
    end

    links { "Common", "ReflectCpp" }

    filter "configurations:Debug"
        symbols "On"

    filter "configurations:Release"
        optimize "On"

    filter "system:windows"
        defines { "DESERT_PLATFORM_WINDOWS" }

    filter "system:macosx"
        defines { "DESERT_PLATFORM_MACOS" }
        -- Common contains Objective-C (file dialog); linking it needs AppKit + the ObjC runtime.
        links { "Cocoa.framework", "Foundation.framework" }

    filter {}
