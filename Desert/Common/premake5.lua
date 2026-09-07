project "Common"
    kind "StaticLib"

    files {
        "Source/Common/**.cpp",
        "Source/Common/**.hpp",
        -- The shared project-format serializer lives in the desert-shared submodule (one definition
        -- for the engine and the launcher); the ENGINE compiles it into Common, the launcher
        -- compiles the same file itself. Missing file here means an uninitialized submodule:
        -- `git submodule update --init ThirdParty/desert-shared`.
        "%{wks.location}/ThirdParty/desert-shared/Source/ProjectFormat.cpp",
        "%{wks.location}/ThirdParty/desert-shared/Source/EngineRegistry.cpp",
    }

    includedirs {
        "Source/",
        "Source/Common",
    }
    
    for name, path in pairs(deps.Common.IncludeDir) do
        externalincludedirs { path }
    end
    
    for name, path in pairs(deps.CommonSpecific.IncludeDir) do
        externalincludedirs { path }
    end
    
    links { deps.Common.Libraries.yaml_cpp, "Optick" }
    
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

    -- The Source/Common/** glob above already picks the Windows platform sources
    -- up on every OS — drop them when not targeting Windows.
    filter { "system:not windows" }
        removefiles {
            "Source/Common/Platform/Windows/**",
        }

    filter { "system:macosx" }
        defines { "DESERT_PLATFORM_MACOS" }
        files {
            "Source/Common/Platform/MacOS/**.cpp",
            "Source/Common/Platform/MacOS/**.hpp",
            "Source/Common/Platform/MacOS/**.mm",
        }

    filter {}