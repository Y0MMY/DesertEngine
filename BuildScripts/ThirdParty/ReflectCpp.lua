-- reflect-cpp support library for non-Windows platforms.
--
-- The vendored ThirdParty/reflect-cpp/include headers are reflect-cpp v0.19.0
-- (repacked under rflcpp/); they are NOT header-only — rfl::Generic, the JSON
-- reader/writer and yyjson live in compiled sources. On Windows those come
-- prebuilt as reflectcpp.lib; here we build them from the matching v0.19.0
-- sources, fetched into ThirdParty/reflect-cpp/src by scripts/MacOS/Setup.sh.

local root = _MAIN_SCRIPT_DIR .. "/ThirdParty/reflect-cpp"

if not os.isfile( root .. "/src/reflectcpp.cpp" ) then
    error( "ThirdParty/reflect-cpp/src is missing. Run scripts/MacOS/Setup.sh to fetch the reflect-cpp v0.19.0 sources." )
end

project "ReflectCpp"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    location ( _MAIN_SCRIPT_DIR .. "/build/Projects/ReflectCpp" )

    files
    {
        root .. "/src/reflectcpp.cpp",      -- amalgamation: rfl core (Generic, generic reader/writer, schema)
        root .. "/src/reflectcpp_json.cpp", -- JSON backend (the engine only uses rfl::json)
        root .. "/src/yyjson.c",
    }

    includedirs
    {
        -- "rfl/..." headers — the vendored copy, so the lib exactly matches
        -- what the engine compiles against.
        root .. "/include/rflcpp",
        root .. "/include/rflcpp/rfl/thirdparty", -- yyjson.h for yyjson.c
    }

    pic "On"

    filter {}
