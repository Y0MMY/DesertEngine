local baseDir = "%{wks.location}/ThirdParty"

-- Assimp: prebuilt MSVC binaries on Windows (vendored in Editor/ThirdParty),
-- Homebrew's libassimp on macOS. Include dir must match the linked binary, so
-- both are resolved together.
local function assimpIncludeDir()
    if os.target() == "macosx" and DesertPlatform.HomebrewPrefix then
        return DesertPlatform.HomebrewPrefix .. "/include"
    end
    return "ThirdParty/assimp/include"
end

local function assimpLib(config)
    if os.target() == "macosx" then
        -- Link by name; the Homebrew lib dir is set workspace-wide in PlatformMacOS.lua.
        return "assimp"
    end
    if config == "Debug" then
        return "ThirdParty/assimp/bin/Debug/assimp-vc142-mtd.lib"
    end
    return "ThirdParty/assimp/bin/Release/assimp-vc142-mt.lib"
end

Dependencies = {

    EditorSpecific = {
         IncludeDir = {
            imGuizmo   = "ThirdParty/ImGuizmo",
            assimp     = assimpIncludeDir(),
            stb        = baseDir .. "/stb/include",
            reflect_cpp = baseDir .. "/reflect-cpp/include",
        },

        Libraries = {
            Debug = {
                assimp = assimpLib("Debug"),
            },

            Release = {
                assimp = assimpLib("Release"),
            }
        }
    }
}

return Dependencies
