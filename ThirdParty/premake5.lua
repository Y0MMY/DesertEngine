-- ThirdParty projects. The project definitions live in BuildScripts/ThirdParty/
-- (the submodules themselves don't carry premake files), so they can be
-- versioned with the engine and shared across platforms.

local buildScripts = _MAIN_SCRIPT_DIR .. "/BuildScripts/ThirdParty"

include( buildScripts .. "/GLFW.lua" )
include( buildScripts .. "/ImGui.lua" )
include( buildScripts .. "/ImGuiNodeEditor.lua" )
include( buildScripts .. "/YamlCpp.lua" )
include( buildScripts .. "/Jolt.lua" )
include( buildScripts .. "/Lua.lua" )
include( buildScripts .. "/Optick.lua" )
include( buildScripts .. "/MeshOptimizer.lua" )
include( buildScripts .. "/Dlib.lua" ) -- optional; no-op when ThirdParty/dlib is absent

-- Windows links the prebuilt reflectcpp.lib; other platforms compile the
-- yyjson backend from source (see the file for details).
if os.target() ~= "windows" then
    include( buildScripts .. "/ReflectCpp.lua" )
end
