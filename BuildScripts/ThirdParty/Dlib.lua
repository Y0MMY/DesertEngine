-- dlib (davisking/dlib, Boost license). Face detection + 68-point landmark tracking for the
-- Model-from-Camera panel's live camera overlay. OPTIONAL and NOT cloned by Setup.sh (keeps CI light):
-- clone https://github.com/davisking/dlib into ThirdParty/dlib to enable real tracking. When present the
-- Editor auto-defines DESERT_WITH_DLIB and links this lib; when absent Editor/Widgets/FaceTracker compiles
-- as a no-op stub, so a fresh checkout / CI still builds.

local root = _MAIN_SCRIPT_DIR .. "/ThirdParty/dlib"

if not os.isfile( root .. "/dlib/all/source.cpp" ) then
    return -- optional dependency; skip silently when not cloned
end

project "Dlib"
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"
    location ( _MAIN_SCRIPT_DIR .. "/build/Projects/" .. "Dlib" )

    -- dlib's single amalgamated TU. The face detector + shape predictor themselves are header templates,
    -- instantiated in FaceTracker.cpp; this TU provides the non-template runtime (threads, base64, ...).
    files { root .. "/dlib/all/source.cpp" }

    includedirs { root }

    -- No X11/GUI, and no image-file codecs (we feed raw camera pixels, never decode files here) so dlib
    -- needs no external libjpeg/png/BLAS to build.
    defines { "DLIB_NO_GUI_SUPPORT" }

    filter "system:windows"
        systemversion "latest"

    filter "system:not windows"
        pic "On"

    filter {}
