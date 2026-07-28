-- Jolt Physics static library, built from the ThirdParty/JoltPhysics submodule.
-- No optional JPH_* feature defines: consumers (the Desert project) compile the
-- Jolt headers with default settings, and the library must match them.

local root = _MAIN_SCRIPT_DIR .. "/ThirdParty/JoltPhysics"

project "Jolt"
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"
    location ( _MAIN_SCRIPT_DIR .. "/build/Projects/" .. "Jolt" )

    files
    {
        root .. "/Jolt/**.cpp",
        root .. "/Jolt/**.h",
    }

    includedirs { root }

    filter "system:windows"
        systemversion "latest"

    filter "system:not windows"
        pic "On"

    filter {}
