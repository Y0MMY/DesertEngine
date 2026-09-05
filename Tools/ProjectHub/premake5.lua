-- ProjectHub — standalone project launcher (Unity Hub-style), fully SEPARATE from the Editor.
-- A small GLFW + ImGui (OpenGL2 backend) window: lists recent projects, creates new ones, and
-- launches the Editor with `--project <path>`. Links no engine/renderer code — only GLFW + ImGui +
-- the OS GL, plus Common (+ ReflectCpp) for the shared project-format serializer
-- (Common/Project/ProjectFormat.hpp): the .deproj and projects.json the hub writes must be the
-- bytes the Editor parses, so the two go through one implementation.
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
        -- Common: the shared build-version identity (Common::Version) and the shared project-format
        -- header (Common/Project/ProjectFormat.hpp). The rfl headers stay OUT of the hub's sources —
        -- serialization is compiled once, inside Common.
        "%{wks.location}/Desert/Common/Source/",
        "%{wks.location}/ThirdParty/spdlog/include/",
    }

    links {
        "ImGui",
        "GLFW",
        "Common",
        "ReflectCpp", -- Common/Project/ProjectFormat.cpp serializes via rfl::json
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
            "Foundation.framework",
        }
        -- The OpenGL2 backend uses the (deprecated but present) system GL — silence the warning spam.
        defines { "GL_SILENCE_DEPRECATION" }

    filter "system:windows"
        links { "opengl32" }

    filter {}
