-- Config entry point for Desert Engine.
-- Shared settings are split into BuildScripts/:
--   Platform.lua        — platform detection helpers (DesertPlatform)
--   Workspace.lua       — the workspace + settings common to every project
--   Configurations.lua  — Debug / Release configurations
--   PlatformWindows.lua — Windows-wide settings (x64)
--   PlatformMacOS.lua   — macOS-wide settings (Apple Silicon / ARM64)

include "BuildScripts/Platform.lua"
include "BuildScripts/Workspace.lua"
include "BuildScripts/Configurations.lua"
include "BuildScripts/PlatformWindows.lua"
include "BuildScripts/PlatformMacOS.lua"

group "ThirdParty"
include "ThirdParty/"
group ""

group "Tools"
include "Tools/DesertHeaderTool/"
include "Tools/FbxMeshSplitter/"
include "Tools/ProjectHub/"
include "Tools/DShaderTool/"
include "Tools/PakTool/"
include "Tools/SceneMigrator/"
include "Tools/ImageStat/"
include "Tools/LineJump/"
group ""

include "Desert/"

-- AFTER Desert/, which is where the `deps` table is defined. Every tool above it is dependency-free or
-- vendors its own single header; this one compiles an engine source that uses glm and the Result type,
-- so it needs the same include paths the engine has.
group "Tools"
include "Tools/CloudVolumeBaker/"
include "Tools/CloudLayoutBaker/"
include "Tools/LatticePeak/"
group ""

include "Editor/"
include "Runtime/"
