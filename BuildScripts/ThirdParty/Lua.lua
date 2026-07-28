-- Lua static library, built from the ThirdParty/lua submodule sources.
-- lua.c (standalone interpreter), onelua.c (amalgamation) and ltests.c
-- (upstream test harness) are excluded: the engine embeds Lua via sol2.

local root = _MAIN_SCRIPT_DIR .. "/ThirdParty/lua"

project "Lua"
    kind "StaticLib"
    language "C"
    location ( _MAIN_SCRIPT_DIR .. "/build/Projects/" .. "Lua" )

    files
    {
        root .. "/*.c",
        root .. "/*.h",
    }

    removefiles
    {
        root .. "/lua.c",
        root .. "/onelua.c",
        root .. "/ltests.c",
    }

    filter "system:windows"
        systemversion "latest"

    filter "system:macosx"
        pic "On"
        defines { "LUA_USE_MACOSX" }

    filter "system:linux"
        pic "On"
        defines { "LUA_USE_LINUX" }

    filter {}
