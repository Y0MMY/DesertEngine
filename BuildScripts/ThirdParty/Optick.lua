-- Optick CPU profiler core. Sources live in ThirdParty/optick (cloned by
-- scripts/MacOS/Setup.sh or checked out manually — the dir is not a submodule).
-- The defines MUST stay in sync with Dependencies.lua (Common.Defines) so
-- optick.h compiles identically in the library and in every consumer.

local root = _MAIN_SCRIPT_DIR .. "/ThirdParty/optick"

if not os.isfile( root .. "/src/optick.h" ) then
    error( "ThirdParty/optick sources are missing. Run scripts/MacOS/Setup.sh (macOS) " ..
           "or clone https://github.com/bombomby/optick into ThirdParty/optick." )
end

project "Optick"
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"
    location ( _MAIN_SCRIPT_DIR .. "/build/Projects/" .. "Optick" )

    files
    {
        root .. "/src/**.cpp",
        root .. "/src/**.h",
    }

    includedirs { root .. "/src" }

    defines
    {
        "USE_OPTICK=1",
        "OPTICK_ENABLE_GPU=0",
        "OPTICK_ENABLE_TRACING=0",
    }

    filter "system:windows"
        systemversion "latest"

    filter "system:not windows"
        pic "On"

    filter {}
