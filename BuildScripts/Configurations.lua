-- Debug / Release build configurations (workspace scope, applies to all projects).

filter "configurations:Debug"
    runtime "Debug"
    symbols "On"

filter "configurations:Release"
    runtime "Release"
    optimize "On"
    defines { "NDEBUG" }

filter {}
