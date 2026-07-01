#pragma once

#include <Common/Core/Constants.hpp>

#include <filesystem>
#include <string>
#include <system_error>

// Single source of truth for "source asset path -> deterministic cooked path". This logic used to be copied
// in ImportManager, MeshDnD, TextureImporter and FileExplorerPanel and DRIFTED apart (that drift caused the
// "textures outside Resources/Textures silently failed to cook" bug). All callers route through here now.
// These are PURE (no directory creation) — callers create_directories before writing.
namespace Desert::Editor::CookPaths
{
    // Source mesh -> Cooked/Meshes/<rel>.<ext> (ext = ".stmesh" / ".skmesh" / "_<anim>.anim" / ".skeleton").
    // Meshes under Resources/Assets/Meshes keep their layout (relative to that dir). Mesh sources ANYWHERE ELSE
    // under content (e.g. a character pack in Resources/Assets/Collections/<pack>/) map relative to Assets/
    // (then Resources/) instead — otherwise the relative path escapes Cooked/Meshes with "../" and the cooked
    // outputs land outside the tree the preloader scans, so the asset is silently never discovered.
    inline std::filesystem::path CookedMesh( const std::filesystem::path& source, const std::string& ext )
    {
        namespace fs = std::filesystem;
        std::error_code ec;

        fs::path   rel       = fs::relative( source, Common::Constants::Path::MESH_PATH, ec );
        const bool underMesh = !rel.empty() && rel.begin()->string() != "..";
        if ( !underMesh )
        {
            fs::path relAssets = fs::relative( source, Common::Constants::Path::ASSETS_PATH, ec );
            if ( !relAssets.empty() && relAssets.begin()->string() != ".." )
                rel = relAssets;
            else
            {
                fs::path relRes = fs::relative( source, Common::Constants::Path::RESOURCE_PATH, ec );
                if ( !relRes.empty() && relRes.begin()->string() != ".." )
                    rel = relRes;
            }
        }

        fs::path result = Common::Constants::Path::MESH_PATH_COOKED / rel;
        result.replace_extension( ext );
        return result;
    }

    // Source texture -> Cooked/Textures/<rel>.<ext>. Textures under Resources/Assets/Textures keep their
    // layout (relative to that dir, so handles/paths are stable); textures ANYWHERE ELSE under Resources/
    // (e.g. a pack's Assets/Collections/<pack>/textures/) map relative to Resources/ instead — otherwise the
    // relative path escapes Cooked/Textures with "../" and the cook silently fails.
    inline std::filesystem::path CookedTexture( const std::filesystem::path& source, const std::string& ext )
    {
        namespace fs = std::filesystem;

        fs::path   rel      = fs::relative( source, Common::Constants::Path::TEXTUREDIR_PATH );
        const bool underTex = !rel.empty() && rel.begin()->string() != "..";
        if ( !underTex )
        {
            const fs::path relRes = fs::relative( source, Common::Constants::Path::RESOURCE_PATH );
            if ( !relRes.empty() && relRes.begin()->string() != ".." )
                rel = relRes;
        }

        fs::path result = Common::Constants::Path::TEXTURE_PATH_COOKED / rel;
        result.replace_extension( ext );
        return result;
    }
} // namespace Desert::Editor::CookPaths
