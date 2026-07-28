-- FbxMeshSplitter — standalone CLI: splits a multi-mesh FBX into individual .obj files + a collection
-- manifest. Depends only on Assimp (no engine code). The launcher will invoke it; runnable by hand too.
project "FbxMeshSplitter"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"

    files {
        "**.cpp",
        "**.hpp",
    }

    filter "configurations:Debug"
        symbols "On"

    filter "configurations:Release"
        optimize "On"

    -- Assimp: prebuilt MSVC binaries on Windows, Homebrew's libassimp on macOS.
    filter "system:windows"
        includedirs { "%{wks.location}/Editor/ThirdParty/assimp/include" }

    filter { "system:windows", "configurations:Debug" }
        libdirs { "%{wks.location}/Editor/ThirdParty/assimp/bin/Debug" }
        links   { "assimp-vc142-mtd" }
        -- project-relative (Tools/FbxMeshSplitter -> ../../ = workspace root); %{wks.location} misexpands here.
        postbuildcommands {
            '{COPYFILE} "../../Editor/ThirdParty/assimp/bin/Debug/assimp-vc142-mtd.dll" "%{cfg.targetdir}/assimp-vc142-mtd.dll"'
        }

    filter { "system:windows", "configurations:Release" }
        libdirs { "%{wks.location}/Editor/ThirdParty/assimp/bin/Release" }
        links   { "assimp-vc142-mt" }
        postbuildcommands {
            '{COPYFILE} "../../Editor/ThirdParty/assimp/bin/Release/assimp-vc142-mt.dll" "%{cfg.targetdir}/assimp-vc142-mt.dll"'
        }

    filter "system:macosx"
        includedirs { (DesertPlatform.HomebrewPrefix or "/usr/local") .. "/include" }
        links { "assimp" }

    filter {}
