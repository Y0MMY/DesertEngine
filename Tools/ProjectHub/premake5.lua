-- ProjectHub — standalone project launcher (Unity Hub-style), fully SEPARATE from the Editor.
-- A small GLFW + ImGui (OpenGL2 backend) window: a GRID of recent projects with thumbnails, search
-- and a context menu; New Project from templates scanned off the engine install; Project Settings.
-- Links NO engine code at all (R1) — only GLFW + ImGui + stb_image + the OS GL + ReflectCpp.
-- The project formats and the launch protocol come from the
-- desert-shared submodule: the hub compiles the shared serializer itself, exactly as the future
-- standalone launcher repository will, so cutting the hub out of this repo (L3) moves files and
-- changes no dependencies.
project "ProjectHub"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"

    files {
        -- Flat glob on purpose: Source/Platform/ holds one file per OS and each is added by the
        -- filter that wants it, so a `**` here would hand the Windows COM dialog to clang.
        "Source/*.cpp",
        "Source/*.hpp",
        -- ImGui platform backends compiled directly into the hub (the ImGui static lib holds core only).
        "%{wks.location}/ThirdParty/ImGui/backends/imgui_impl_glfw.cpp",
        "%{wks.location}/ThirdParty/ImGui/backends/imgui_impl_opengl2.cpp",
        -- The shared project-format serializer, compiled by every host for itself (the engine has
        -- its own copy inside libCommon; linking that would drag the whole engine Common back in).
        "%{wks.location}/ThirdParty/desert-shared/Source/ProjectFormat.cpp",
        -- engines.json: the engine writes it at startup, the launcher reads it to find the
        -- engine at all — after L3 there is no DESERT_ROOT exported for the launcher.
        "%{wks.location}/ThirdParty/desert-shared/Source/EngineRegistry.cpp",
        -- stb_image: project and template thumbnails are PNGs on disk. The launcher decodes them
        -- itself; the ONE file that turns the bytes into something ImGui can draw is the twenty-line
        -- backend at the bottom of Main.cpp, and it is the only place a graphics API is named.
        "%{wks.location}/ThirdParty/stb/stb_image.cpp",
    }

    includedirs {
        -- desert-shared: formats + launch protocol + ResultStr. Its host-supplied dependencies:
        -- reflect-cpp (the serializer) and the fmt headers (ResultStr) via the vendored spdlog.
        "%{wks.location}/ThirdParty/desert-shared/Include/",
    }
    externalincludedirs {
        "%{wks.location}/ThirdParty/ImGui/",
        "%{wks.location}/ThirdParty/GLFW/include/",
        "%{wks.location}/ThirdParty/reflect-cpp/include/",
        "%{wks.location}/ThirdParty/spdlog/include/",
        "%{wks.location}/ThirdParty/stb/include/",
    }

    links {
        "ImGui",
        "GLFW",
        "ReflectCpp", -- desert-shared's ProjectFormat.cpp serializes via rfl::json
    }

    filter "configurations:Debug"
        symbols "On"

    filter "configurations:Release"
        optimize "On"

    filter "system:macosx"
        files { "Source/Platform/FileDialog_Mac.mm" }
        links {
            "Cocoa.framework",
            "IOKit.framework",
            "CoreFoundation.framework",
            "CoreVideo.framework",
            "OpenGL.framework",
            -- NSOpenPanel + the UTType its allowedContentTypes takes (Source/Platform/FileDialog_Mac.mm)
            "AppKit.framework",
            "UniformTypeIdentifiers.framework",
            "Foundation.framework",
        }
        -- The OpenGL2 backend uses the (deprecated but present) system GL — silence the warning spam.
        defines { "GL_SILENCE_DEPRECATION" }

    filter "system:windows"
        files { "Source/Platform/FileDialog_Windows.cpp" }
        links { "opengl32", "ole32", "shell32" } -- IFileOpenDialog (Source/Platform/FileDialog_Windows.cpp)

    filter {}
