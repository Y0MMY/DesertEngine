#pragma once

#include <filesystem>
#include <string>

namespace Common::Constants
{
    namespace Path
    {
        // Layout (UE-like): engine/editor resources (Shaders, Fonts) live under the shared Resources/
        // tree next to the binary's working directory; all USER CONTENT lives under the PROJECT's assets
        // root. By default (no project) the content paths point at Resources/Assets/ — the built-in
        // sandbox; opening a .deproj calls SetProjectRoot() and REMAPS every content path (and the Cooked/
        // intermediate tree) into the project folder. Engine resources are never remapped.

        // --- Engine / editor resources (SHARED, never remapped) ---
        inline const std::filesystem::path RESOURCE_PATH         = "Resources/";
        inline const std::filesystem::path SHADERDIR_PATH        = "Resources/Shaders/";
        inline const std::filesystem::path RESOURCE_SPIRV_BINARY = "Resources/Shaders/SPIRV/Bin/";
        inline const std::filesystem::path FONTS_PATH            = "Resources/Fonts/";
        // Built-in vector icons (.svg, imported into SDF at first use — see Runtime::IconService).
        inline const std::filesystem::path ICONS_PATH = "Resources/Icons/";

        // --- User content (PROJECT-owned; defaults = built-in sandbox) ---
        inline std::filesystem::path ASSETS_PATH        = "Resources/Assets/";
        inline std::filesystem::path MESH_PATH          = "Resources/Assets/Meshes/";
        inline std::filesystem::path MATERIAL_PATH      = "Resources/Assets/Materials/";
        inline std::filesystem::path TEXTUREDIR_PATH    = "Resources/Assets/Textures/";
        inline std::filesystem::path TEXTUREDIRENV_PATH = "Resources/Assets/Textures/Cubes/";
        inline std::filesystem::path SKYBOX_PATH        = "Resources/Assets/Textures/HDR/";
        inline std::filesystem::path SCENE_PATH         = "Resources/Assets/Scenes/";
        inline std::filesystem::path PREFAB_PATH        = "Resources/Assets/Prefabs/";
        inline std::filesystem::path SCRIPT_PATH        = "Resources/Assets/Scripts/";
        inline std::filesystem::path COLLECTIONS_PATH   = "Resources/Assets/Collections/";
        // Cloud noise volumes (`.dcnv`) the artist bakes in the Cloud Noise Volume panel. Its own folder
        // rather than Textures/ because a volume is not a texture to this engine: it has no importer, no
        // cooked twin and no 2D preview, and mixing it into the texture scan would offer it in every
        // texture slot in the editor.
        inline std::filesystem::path CLOUD_NOISE_PATH = "Resources/Assets/Clouds/";

        // --- Cooked / intermediate (generated; PROJECT-owned) ---
        inline std::filesystem::path COOKED_PATH         = "Cooked/";
        inline std::filesystem::path MESH_PATH_COOKED    = "Cooked/Meshes/";
        inline std::filesystem::path TEXTURE_PATH_COOKED = "Cooked/Textures/";

        // Points every content path at <projectDir>/<assetsRoot>/... and the cooked tree at
        // <projectDir>/Cooked/. Must be called BEFORE any subsystem reads the paths (the editor does it
        // while parsing --project, before the engine spins up). assetsRoot comes from the .deproj — the
        // built-in sandbox project uses "Resources/Assets" so the historical layout stays byte-identical.
        inline void SetProjectRoot( const std::filesystem::path& projectDir,
                                    const std::filesystem::path& assetsRoot )
        {
            const std::filesystem::path assets = ( projectDir / assetsRoot ).lexically_normal();

            ASSETS_PATH        = assets / "";
            MESH_PATH          = assets / "Meshes/";
            MATERIAL_PATH      = assets / "Materials/";
            TEXTUREDIR_PATH    = assets / "Textures/";
            TEXTUREDIRENV_PATH = assets / "Textures/Cubes/";
            SKYBOX_PATH        = assets / "Textures/HDR/";
            SCENE_PATH         = assets / "Scenes/";
            PREFAB_PATH        = assets / "Prefabs/";
            SCRIPT_PATH        = assets / "Scripts/";
            COLLECTIONS_PATH   = assets / "Collections/";
            CLOUD_NOISE_PATH   = assets / "Clouds/";

            const std::filesystem::path cooked = ( projectDir / "Cooked" ).lexically_normal();
            COOKED_PATH                        = cooked / "";
            MESH_PATH_COOKED                   = cooked / "Meshes/";
            TEXTURE_PATH_COOKED                = cooked / "Textures/";
        }
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
