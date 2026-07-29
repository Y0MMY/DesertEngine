-- meshoptimizer (zeux/meshoptimizer, MIT). Mesh simplification / LOD generation +
-- vertex/index optimization. Sources live in ThirdParty/meshoptimizer (cloned by
-- scripts/MacOS/Setup.sh — not a submodule). Single public header src/meshoptimizer.h.

local root = _MAIN_SCRIPT_DIR .. "/ThirdParty/meshoptimizer"

if not os.isfile( root .. "/src/meshoptimizer.h" ) then
    error( "ThirdParty/meshoptimizer sources are missing. Run scripts/MacOS/Setup.sh (macOS) " ..
           "or clone https://github.com/zeux/meshoptimizer into ThirdParty/meshoptimizer." )
end

project "MeshOptimizer"
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"
    location ( _MAIN_SCRIPT_DIR .. "/build/Projects/" .. "MeshOptimizer" )

    files
    {
        root .. "/src/**.cpp",
        root .. "/src/**.h",
    }

    includedirs { root .. "/src" }

    filter "system:windows"
        systemversion "latest"

    filter "system:not windows"
        pic "On"

    filter {}
