-- ProjectHub — standalone project launcher (Unity Hub-style), fully SEPARATE from the Editor.
-- A small GLFW + ImGui (OpenGL2 backend) window: lists recent projects, creates new ones, and
-- launches the Editor with `--project <path>`. Links no engine code — only GLFW + ImGui + the OS GL.
project "ProjectHub"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"

    files {
        "Source/**.cpp",
        "Source/**.hpp",
        -- ImGui platform backends compiled directly into the hub (the ImGui static lib holds core only).
        "%{wks.location}/ThirdParty/ImGui/backends/imgui_impl_glfw.cpp",
        "%{wks.location}/ThirdParty/ImGui/backends/imgui_impl_opengl2.cpp",
    }

    includedirs {
        "%{wks.location}/ThirdParty/ImGui/",
        "%{wks.location}/ThirdParty/GLFW/include/",
    }

    links {
        "ImGui",
        "GLFW",
    }

    filter "configurations:Debug"
        symbols "On"

    filter "configurations:Release"
        optimize "On"

    filter "system:macosx"
        links {
            "Cocoa.framework",
            "IOKit.framework",
            "CoreFoundation.framework",
            "CoreVideo.framework",
            "OpenGL.framework",
        }
        -- The OpenGL2 backend uses the (deprecated but present) system GL — silence the warning spam.
        defines { "GL_SILENCE_DEPRECATION" }

    filter "system:windows"
        links { "opengl32" }

    filter {}
