-- GoogleTest, compiled from the vendored v1.17.0 sources — Windows only.
--
-- The repo used to carry PREBUILT gtest libraries for Windows, and only the Debug one
-- (gtestd.lib) was ever committed: the Windows Release job could not link a single test
-- project since the day tests existed, and every red run said so as LNK1181. A binary
-- nobody can rebuild is not a dependency, it is a time bomb — so Windows now compiles
-- gtest from source exactly the way every other platform-built third-party does
-- (see ReflectCpp.lua for the same story with reflect-cpp).
--
-- macOS/Linux keep linking the package manager's gtest by name (PlatformMacOS.lua sets
-- the lib dirs), so this project is only included on Windows — see ThirdParty/premake5.lua.
--
-- gtest-all.cc is upstream's own amalgamation: it #includes the other src/*.cc, which is
-- why the src directory must be on the include path and why it is the only file listed.

local root = _MAIN_SCRIPT_DIR .. "/ThirdParty/google-test"

if not os.isfile( root .. "/src/gtest-all.cc" ) then
    error( "ThirdParty/google-test/src is missing. The googletest v1.17.0 sources are vendored; restore them from git." )
end

project "GoogleTest"
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"
    location ( _MAIN_SCRIPT_DIR .. "/build/Projects/GoogleTest" )

    files
    {
        root .. "/src/gtest-all.cc",
    }

    includedirs
    {
        root .. "/include",
        root, -- gtest-all.cc includes "src/gtest-*.cc" relative to the gtest root
    }

    filter {}
