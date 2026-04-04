#pragma once

#include <filesystem>
#include <string>

namespace Common::Constants // TODO: should be merge from config
{
    namespace Path
    {
        const std::filesystem::path RESOURCE_PATH         = "Resources/";
        const std::filesystem::path SHADERDIR_PATH        = "Resources/Shaders/";
        const std::filesystem::path TEXTUREDIR_PATH       = "Resources/Textures/";
        const std::filesystem::path TEXTUREDIRENV_PATH    = "Resources/Textures/Cubes/";
        const std::filesystem::path RESOURCE_SPIRV_BINARY = "Resources/Shaders/SPIRV/Bin/";
        const std::filesystem::path SCENE_PATH            = "Resources/Assets/Scene/";
        const std::filesystem::path MATERIAL_PATH         = "Resources/Assets/Material/";
        const std::filesystem::path MESH_PATH             = "Resources/Mesh/";
        const std::filesystem::path MESH_PATH_COOKED      = "Cooked/Meshes/";
        const std::filesystem::path SKYBOX_PATH           = "Resources/Textures/HDR/";
    } // namespace Path

    namespace Extensions
    {
        const std::string SPIRV_BINARY_EXTENSION_VERT = ".spvbin_vert";
        const std::string SPIRV_BINARY_EXTENSION_FRAG = ".spvbin_frag";
        const std::string SPIRV_BINARY_EXTENSION_COMP = ".spvbin_comp";
        const std::string SCENE_EXTENSION             = ".desce";
        const std::string MESH_SERIALIZBLE_EXTENSION  = ".demesh";
        const std::string MATERIAL_EXTENSION          = ".demat";
        const std::string STATIC_MESH                 = ".skmesh";
        const std::string SKINNED_MESH                = ".stmesh";
    } // namespace Extensions
} // namespace Common::Constants