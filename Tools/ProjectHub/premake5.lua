-- ProjectHub — standalone project launcher (Unity Hub-style), fully SEPARATE from the Editor.
-- A small GLFW + ImGui (OpenGL2 backend) window: lists recent projects, creates new ones, and
-- launches the Editor via the shared launch protocol. Links NO engine code at all (R1) — only
-- GLFW + ImGui + the OS GL + ReflectCpp. The project formats and the protocol come from the
-- desert-shared submodule: the hub compiles the shared serializer itself, exactly as the future
-- standalone launcher repository will, so cutting the hub out of this repo (L3) moves files and
-- changes no dependencies.
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
        -- The shared project-format serializer, compiled by every host for itself (the engine has
        -- its own copy inside libCommon; linking that would drag the whole engine Common back in).
        "%{wks.location}/ThirdParty/desert-shared/Source/ProjectFormat.cpp",
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
