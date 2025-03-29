local baseDir = "%{wks.location}/ThirdParty"
local assimpBase =      baseDir ..          "/assimp/bin"
local shadercBase =     baseDir ..          "/VulkanSDK/shaderc/Lib"
local spirvCrossBase =  baseDir ..          "/VulkanSDK/spirv_cross/Lib"
local vulkanSDKBase =   baseDir ..          "/VulkanSDK/Lib"

IncludeDir = {}
IncludeDir["entt"] = "%{wks.location}/ThirdParty/entt/include"

LibraryDir = {
    assimp_D = assimpBase .. "/Debug/assimp-vc142-mtd.lib",
    shaderc_Debug = shadercBase .. "/shadercd.lib",
    shaderc_shared_Debug = shadercBase .. "/shaderc_sharedd.lib",
    -- shaderc_shared = shadercBase .. "/shaderc_shared.lib",
    spirv_cross_core_Debug = spirvCrossBase .. "/spirv-cross-cored.lib",
    spirv_cross_glsl_Debug = spirvCrossBase .. "/spirv-cross-glsld.lib",
    vulkan1 = vulkanSDKBase .. "/vulkan-1.lib",
    OGLCompiler_Debug = vulkanSDKBase .. "/OGLCompilerd.lib",
    shaderc_combined_Debug = shadercBase .. "/shaderc_combinedd.lib",
    shaderc_util_Debug = shadercBase .. "/shaderc_utild.lib",
}

return {
    IncludeDir = IncludeDir,
    LibraryDir = LibraryDir,
}
