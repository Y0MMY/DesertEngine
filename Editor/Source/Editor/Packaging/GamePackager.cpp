#include "GamePackager.hpp"

#include <Engine/Project/ProjectContext.hpp>

#include <Common/Core/Constants.hpp>
#include <Common/Core/Logger.hpp>
#include <Common/Utilities/FileSystem.hpp>
#include <Common/Utilities/PakFile.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace Desert::Editor
{
    namespace fs = std::filesystem;

    namespace
    {
        // Raw mesh sources are import-time input only — the runtime reads cooked .stmesh/.skmesh.
        bool IsRawMeshSource( const fs::path& p )
        {
            std::string ext = p.extension().string();
            std::transform( ext.begin(), ext.end(), ext.begin(), ::tolower );
            return ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb" ||
                   ext == ".blend" || ext == ".dae";
        }

        struct CopyStats
        {
            size_t   Files = 0;
            uintmax_t Bytes = 0;
        };

        // Recursive copy with an optional per-file filter. Returns false (with error set) on failure.
        bool CopyTree( const fs::path& from, const fs::path& to, bool skipRawMeshSources,
                       CopyStats& stats, std::string& error )
        {
            std::error_code ec;
            if ( !fs::exists( from, ec ) )
                return true; // nothing to copy is fine (e.g. no Cooked/ yet)

            for ( auto it = fs::recursive_directory_iterator( from, ec );
                  it != fs::recursive_directory_iterator(); it.increment( ec ) )
            {
                if ( ec )
                {
                    error = "walk failed under " + from.string() + ": " + ec.message();
                    return false;
                }
                const fs::path& src = it->path();
                if ( !it->is_regular_file() )
                    continue;
                if ( skipRawMeshSources && IsRawMeshSource( src ) )
                    continue;

                const fs::path rel = fs::relative( src, from, ec );
                const fs::path dst = to / rel;
                fs::create_directories( dst.parent_path(), ec );
                fs::copy_file( src, dst, fs::copy_options::overwrite_existing, ec );
                if ( ec )
                {
                    error = "copy failed: " + src.string() + " -> " + dst.string() + ": " + ec.message();
                    return false;
                }
                ++stats.Files;
                stats.Bytes += fs::file_size( dst, ec );
            }
            return true;
        }

        // Streams every file under `from` into the pak as "<keyPrefix>/<relative>". Same filter
        // semantics as CopyTree.
        bool AddTreeToPak( Common::Utils::PakWriter& pak, const fs::path& from,
                           const std::string& keyPrefix, bool skipRawMeshSources, CopyStats& stats,
                           std::string& error )
        {
            std::error_code ec;
            if ( !fs::exists( from, ec ) )
                return true; // nothing to pack is fine (e.g. no Cooked/ yet)

            for ( auto it = fs::recursive_directory_iterator( from, ec );
                  it != fs::recursive_directory_iterator(); it.increment( ec ) )
            {
                if ( ec )
                {
                    error = "walk failed under " + from.string() + ": " + ec.message();
                    return false;
                }
                const fs::path& src = it->path();
                if ( !it->is_regular_file() )
                    continue;
                if ( skipRawMeshSources && IsRawMeshSource( src ) )
                    continue;

                const fs::path rel = fs::relative( src, from, ec );
                const std::string key = keyPrefix + "/" + rel.generic_string();
                if ( !pak.AddFile( key, src ) )
                {
                    error = "pak write failed for " + src.string();
                    return false;
                }
                ++stats.Files;
                stats.Bytes += fs::file_size( src, ec );
            }
            return true;
        }

        std::string SanitizeName( std::string name )
        {
            for ( auto& ch : name )
                if ( ch == ' ' || ch == '/' || ch == '\\' )
                    ch = '_';
            return name.empty() ? std::string( "Game" ) : name;
        }
    } // namespace

    PackageResult PackageGame( const PackageOptions& options )
    {
        using Project::ProjectContext;

        if ( !ProjectContext::HasProject() )
            return { false, "No project is open.", "" };

        const std::string projectName = ProjectContext::Current().Name;
        const std::string safeName    = SanitizeName( projectName );

        // 1) The Runtime binary for the chosen configuration (editor cwd is Editor/).
        const fs::path runtimeBin = fs::path( ".." ) / "build" / "Bin" / options.Config / "Runtime";
        std::error_code ec;
        if ( !fs::exists( runtimeBin, ec ) )
            return { false,
                     "Runtime binary not found (" + runtimeBin.string() +
                          "). Build it first: scripts/MacOS/BuildMacOS.sh " + options.Config,
                     "" };

        const fs::path pkgDir = fs::path( options.OutputDir ) / safeName;
        fs::create_directories( pkgDir, ec );
        if ( ec )
            return { false, "Cannot create output dir " + pkgDir.string() + ": " + ec.message(), "" };

        CopyStats   stats;
        std::string error;

        // 2) Player binary.
        fs::copy_file( runtimeBin, pkgDir / "Runtime", fs::copy_options::overwrite_existing, ec );
        if ( ec )
            return { false, "Cannot copy the Runtime binary: " + ec.message(), "" };
        fs::permissions( pkgDir / "Runtime",
                         fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                              fs::perms::others_read | fs::perms::others_exec,
                         ec );
        ++stats.Files;

        // 3) ALL content goes into ONE Content.dpak (UE .pak model): project assets (raw mesh sources
        // stripped — the runtime reads cooked meshes only), the cooked cache and the engine shaders.
        // The Runtime mounts the archive at startup; every content read resolves through the VFS.
        namespace P = Common::Constants::Path;
        {
            Common::Utils::PakWriter pak( pkgDir / "Content.dpak" );
            if ( !pak.IsOpen() )
                return { false, "Cannot create Content.dpak in " + pkgDir.string(), "" };

            if ( !AddTreeToPak( pak, P::ASSETS_PATH, "Assets", /*skipRawMeshSources=*/true, stats, error ) )
                return { false, error, "" };
            if ( !AddTreeToPak( pak, P::COOKED_PATH, "Cooked", false, stats, error ) )
                return { false, error, "" };
            if ( !AddTreeToPak( pak, P::SHADERDIR_PATH, "Resources/Shaders", false, stats, error ) )
                return { false, error, "" };

            if ( pak.Finalize() == 0 )
                return { false, "Failed to finalize Content.dpak (no entries?)", "" };
        }

        // 5) Regenerated .deproj: content now lives under Assets/ next to the binary. The DefaultScene
        // moves with it when it pointed inside the old assets root.
        {
            std::string defaultScene = ProjectContext::Current().DefaultScene;
            const std::string oldRoot = ProjectContext::Current().AssetsRoot;
            if ( !defaultScene.empty() && !oldRoot.empty() && defaultScene.rfind( oldRoot, 0 ) == 0 )
                defaultScene = "Assets" + defaultScene.substr( oldRoot.size() );

            std::ostringstream deproj;
            deproj << "{\"Name\":\"" << projectName << "\",\"AssetsRoot\":\"Assets\",\"DefaultScene\":\""
                   << defaultScene << "\"}";
            Common::Utils::FileSystem::WriteContentToFile( pkgDir / ( safeName + ".deproj" ),
                                                           deproj.str() );
        }

        // 6) Launcher. v1 limitation: MoltenVK/Vulkan loader come from Homebrew on the target machine
        // (same resolution the dev scripts use) — bundling the dylibs is a follow-up.
        {
            std::ostringstream run;
            run << "#!/usr/bin/env bash\n"
                << "# Launches " << projectName << " (packaged by the Desert Editor).\n"
                << "set -euo pipefail\n"
                << "cd \"$(dirname \"$0\")\"\n"
                << "BREW_PREFIX=\"${HOMEBREW_PREFIX:-$(brew --prefix 2>/dev/null || echo /opt/homebrew)}\"\n"
                << "export VK_ICD_FILENAMES=\"${VK_ICD_FILENAMES:-$BREW_PREFIX/etc/vulkan/icd.d/MoltenVK_icd.json}\"\n"
                << "export DYLD_FALLBACK_LIBRARY_PATH=\"$BREW_PREFIX/lib${DYLD_FALLBACK_LIBRARY_PATH:+:$DYLD_FALLBACK_LIBRARY_PATH}\"\n"
                << "exec ./Runtime --project " << safeName << ".deproj \"$@\"\n";
            const fs::path runSh = pkgDir / "run.sh";
            Common::Utils::FileSystem::WriteContentToFile( runSh, run.str() );
            fs::permissions( runSh,
                             fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                                  fs::perms::others_read | fs::perms::others_exec,
                             ec );
        }

        std::ostringstream msg;
        msg << "Packaged '" << projectName << "' -> " << fs::absolute( pkgDir, ec ).string() << "  ("
            << stats.Files << " files, " << ( stats.Bytes / ( 1024 * 1024 ) ) << " MB, " << options.Config
            << " runtime)";
        LOG_INFO( "[Package] {}", msg.str() );
        return { true, msg.str(), fs::absolute( pkgDir, ec ).string() };
    }
} // namespace Desert::Editor
