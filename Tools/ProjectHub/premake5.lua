-- ProjectHub — standalone project launcher (Unity Hub-style), fully SEPARATE from the Editor.
-- A small GLFW + ImGui (VULKAN backend) window: a GRID of recent projects with thumbnails, search
-- and a context menu; New Project from templates scanned off the engine install; Project Settings.
-- Links NO engine code at all (R1) — only GLFW + ImGui + stb_image + Vulkan + ReflectCpp.
-- The project formats and the launch protocol come from the
-- desert-shared submodule: the hub compiles the shared serializer itself, exactly as the future
-- standalone launcher repository will, so cutting the hub out of this repo (L3) moves files and
-- changes no dependencies.
--
-- THIS SCRIPT FINDS VULKAN ITSELF AND DOES NOT USE `Dependencies.lua`, WHICH IS THE POINT. The
-- engine's discovery lives in `Desert/Dependencies.lua`, and the root premake includes this file
-- BEFORE `Desert/` — so the `deps` table does not exist yet here even if we wanted it. More
-- importantly it will not exist at all in the launcher's own repository. Twenty lines duplicated on
-- purpose (contract §3 forbids a second source of truth for a VALUE; this is a lookup of the local
-- machine, and each repository has to do it for itself) against a build script that stops working
-- the day the launcher moves out.
local function findVulkan()
    local sdk = os.getenv("VULKAN_SDK")
    if sdk and sdk ~= "" then
        if os.target() == "windows" then
            return { include = sdk .. "/Include", lib = sdk .. "/Lib" }
        end
        return { include = sdk .. "/include", lib = sdk .. "/lib" }
    end

    if os.target() == "windows" then
        local versions = os.matchdirs((os.getenv("PROGRAMFILES") or "C:/Program Files") .. "/VulkanSDK/*")
        table.sort(versions, function(a, b) return a > b end)
        if #versions > 0 then
            return { include = versions[1] .. "/Include", lib = versions[1] .. "/Lib" }
        end
        return nil
    end

    if os.target() == "macosx" then
        local versions = os.matchdirs((os.getenv("HOME") or "") .. "/VulkanSDK/*")
        table.sort(versions, function(a, b) return a > b end)
        if #versions > 0 and os.isdir(versions[1] .. "/macOS") then
            return { include = versions[1] .. "/macOS/include", lib = versions[1] .. "/macOS/lib" }
        end
    end

    -- Homebrew (macOS) or a distribution's own prefix (Linux).
    for _, prefix in ipairs({ os.getenv("HOMEBREW_PREFIX"), "/opt/homebrew", "/usr/local", "/usr" }) do
        if prefix and os.isfile(prefix .. "/include/vulkan/vulkan.h") then
            return { include = prefix .. "/include", lib = prefix .. "/lib" }
        end
    end
    return nil
end

local vulkan = findVulkan()
if not vulkan then
    -- LOUD, and not a silent skip. A launcher configured without Vulkan headers fails at
    -- `#include <vulkan/vulkan.h>` fifty lines into a compile, which reads as a broken checkout.
    error("ProjectHub: no Vulkan SDK found. Set VULKAN_SDK, or install the Vulkan headers/loader " ..
          "(macOS: brew install vulkan-headers vulkan-loader molten-vk).")
end

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
        "%{wks.location}/ThirdParty/ImGui/backends/imgui_impl_vulkan.cpp",
        -- The shared project-format serializer, compiled by every host for itself (the engine has
        -- its own copy inside libCommon; linking that would drag the whole engine Common back in).
        "%{wks.location}/ThirdParty/desert-shared/Source/ProjectFormat.cpp",
        -- engines.json: the engine writes it at startup, the launcher reads it to find the
        -- engine at all — after L3 there is no DESERT_ROOT exported for the launcher.
        "%{wks.location}/ThirdParty/desert-shared/Source/EngineRegistry.cpp",
        -- stb_image: project and template thumbnails are PNGs on disk. The launcher decodes them
        -- itself; the ONE file that turns the bytes into something ImGui can draw is
        -- Source/VulkanHost.cpp, and it is the only place a graphics API is named.
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
        vulkan.include,
    }
    libdirs { vulkan.lib }

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
            -- QuartzCore: GLFW's cocoa_window.m attaches the CAMetalLayer that the Vulkan surface
            -- is created from. The OpenGL context it used to make instead needed no such link.
            "QuartzCore.framework",
            "Metal.framework",
            -- NSOpenPanel + the UTType its allowedContentTypes takes (Source/Platform/FileDialog_Mac.mm)
            "AppKit.framework",
            "UniformTypeIdentifiers.framework",
            "Foundation.framework",
            -- The Vulkan LOADER, by name. `vulkan.lib` above is its directory; on macOS this is
            -- Homebrew's libvulkan.dylib forwarding to MoltenVK through the ICD.
            "vulkan",
        }

    filter "system:windows"
        files { "Source/Platform/FileDialog_Windows.cpp" }
        -- IFileOpenDialog (Source/Platform/FileDialog_Windows.cpp) + the Vulkan loader import lib.
        links { "ole32", "shell32", "vulkan-1" }

    filter {}
