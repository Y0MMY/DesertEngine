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
include "Tools/CloudVolumeBaker/"
include "Tools/SceneMigrator/"
group ""

include "Desert/"
include "Editor/"
include "Runtime/"
