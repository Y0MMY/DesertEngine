#pragma once

#include <filesystem>
#include <string>

namespace Common::Constants // TODO: should be merge from config
{
    namespace Path
    {
        // Layout (UE-like): Resources/ is the root; Assets/ holds all USER CONTENT (the content browser root),
        // while engine/editor resources (Shaders, Fonts) sit directly under Resources/ alongside it. Cooked/
        // is a separate, generated intermediate tree (mirrors Assets/).
        const std::filesystem::path RESOURCE_PATH         = "Resources/";

        // --- Engine / editor resources (not user content) ---
        const std::filesystem::path SHADERDIR_PATH        = "Resources/Shaders/";
        const std::filesystem::path RESOURCE_SPIRV_BINARY = "Resources/Shaders/SPIRV/Bin/";
        const std::filesystem::path FONTS_PATH            = "Resources/Fonts/";

        // --- User content (everything under Resources/Assets/) ---
        const std::filesystem::path ASSETS_PATH           = "Resources/Assets/";
        const std::filesystem::path MESH_PATH             = "Resources/Assets/Meshes/";
        const std::filesystem::path MATERIAL_PATH         = "Resources/Assets/Materials/";
        const std::filesystem::path TEXTUREDIR_PATH       = "Resources/Assets/Textures/";
        const std::filesystem::path TEXTUREDIRENV_PATH    = "Resources/Assets/Textures/Cubes/";
        const std::filesystem::path SKYBOX_PATH           = "Resources/Assets/Textures/HDR/";
        const std::filesystem::path SCENE_PATH            = "Resources/Assets/Scenes/";
        const std::filesystem::path PREFAB_PATH           = "Resources/Assets/Prefabs/";
        const std::filesystem::path SCRIPT_PATH           = "Resources/Assets/Scripts/";
        const std::filesystem::path COLLECTIONS_PATH      = "Resources/Assets/Collections/";

        // --- Cooked / intermediate (generated) ---
        const std::filesystem::path MESH_PATH_COOKED      = "Cooked/Meshes/";
        const std::filesystem::path TEXTURE_PATH_COOKED   = "Cooked/Textures/";
    } // namespace Path

    namespace Extensions
    {
        const std::string SPIRV_BINARY_EXTENSION_VERT = ".spvbin_vert";
        const std::string SPIRV_BINARY_EXTENSION_FRAG = ".spvbin_frag";
        const std::string SPIRV_BINARY_EXTENSION_COMP = ".spvbin_comp";
        const std::string SCENE_EXTENSION             = ".desce";
        const std::string MESH_SERIALIZBLE_EXTENSION  = ".demesh";
        const std::string MATERIAL_EXTENSION          = ".demat";
        const std::string PREFAB_EXTENSION            = ".deprefab";
        const std::string STATIC_MESH                 = ".skmesh";
        const std::string SKINNED_MESH                = ".stmesh";
    } // namespace Extensions
} // namespace Common::Constants