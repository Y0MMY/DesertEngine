#include "GamePackager.hpp"

#include <Engine/Project/ProjectContext.hpp>

#include <Common/Core/Constants.hpp>
#include <Common/Core/Logger.hpp>
#include <Common/Utilities/FileSystem.hpp>
#include <Common/Utilities/PakFile.hpp>

#include <algorithm>
#include <cstdlib>
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

        // Layout: a macOS .app bundle (default) or a plain folder. Same content either way — the pak
        // keys are mount-root-relative, so whatever directory holds Content.dpak becomes the content
        // root the VFS serves from.
        const bool     bundle  = options.MacAppBundle;
        const fs::path root    = fs::path( options.OutputDir ) / ( bundle ? safeName + ".app" : safeName );
        const fs::path binDir  = bundle ? root / "Contents" / "MacOS" : root;
        const fs::path resDir  = bundle ? root / "Contents" / "Resources" : root;
        const fs::path fwDir   = root / "Contents" / "Frameworks"; // bundle only
        const char*    binName = bundle ? "Runtime-bin" : "Runtime";

        fs::create_directories( binDir, ec );
        fs::create_directories( resDir, ec );
        if ( ec )
            return { false, "Cannot create output dir " + root.string() + ": " + ec.message(), "" };

        CopyStats   stats;
        std::string error;

        auto makeExecutable = [&]( const fs::path& p )
        {
            fs::permissions( p,
                             fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                                  fs::perms::others_read | fs::perms::others_exec,
                             ec );
        };

        // 2) Player binary.
        fs::copy_file( runtimeBin, binDir / binName, fs::copy_options::overwrite_existing, ec );
        if ( ec )
            return { false, "Cannot copy the Runtime binary: " + ec.message(), "" };
        makeExecutable( binDir / binName );
        ++stats.Files;

        // 3) ALL content goes into ONE Content.dpak (UE .pak model): project assets (raw mesh sources
        // stripped — the runtime reads cooked meshes only), the cooked cache and the engine shaders.
        // The Runtime mounts the archive at startup; every content read resolves through the VFS.
        namespace P = Common::Constants::Path;
        {
            Common::Utils::PakWriter pak( resDir / "Content.dpak" );
            if ( !pak.IsOpen() )
                return { false, "Cannot create Content.dpak in " + resDir.string(), "" };

            if ( !AddTreeToPak( pak, P::ASSETS_PATH, "Assets", /*skipRawMeshSources=*/true, stats, error ) )
                return { false, error, "" };
            if ( !AddTreeToPak( pak, P::COOKED_PATH, "Cooked", false, stats, error ) )
                return { false, error, "" };
            if ( !AddTreeToPak( pak, P::SHADERDIR_PATH, "Resources/Shaders", false, stats, error ) )
                return { false, error, "" };

            if ( pak.Finalize() == 0 )
                return { false, "Failed to finalize Content.dpak (no entries?)", "" };
        }

        // 4) Regenerated .deproj: content now lives under Assets/ next to the pak. The DefaultScene
        // moves with it when it pointed inside the old assets root.
        {
            std::string defaultScene = ProjectContext::Current().DefaultScene;
            const std::string oldRoot = ProjectContext::Current().AssetsRoot;
            if ( !defaultScene.empty() && !oldRoot.empty() && defaultScene.rfind( oldRoot, 0 ) == 0 )
                defaultScene = "Assets" + defaultScene.substr( oldRoot.size() );

            std::ostringstream deproj;
            deproj << "{\"Name\":\"" << projectName << "\",\"AssetsRoot\":\"Assets\",\"DefaultScene\":\""
                   << defaultScene << "\"}";
            Common::Utils::FileSystem::WriteContentToFile( resDir / ( safeName + ".deproj" ),
                                                           deproj.str() );
        }

        // 5) Bundle only: MoltenVK + the Vulkan loader travel INSIDE Contents/Frameworks so the player
        // machine needs no Homebrew. The ICD json is rewritten to point at the bundled dylib (the
        // loader resolves library_path relative to the json file).
        bool bundledVulkan = false;
        if ( bundle )
        {
            const char*    envPrefix = std::getenv( "HOMEBREW_PREFIX" );
            const fs::path brew      = envPrefix ? fs::path( envPrefix ) : fs::path( "/opt/homebrew" );

            const fs::path loaderSrc = brew / "lib" / "libvulkan.1.dylib";
            const fs::path mvkSrc    = brew / "lib" / "libMoltenVK.dylib";
            const fs::path icdSrc    = brew / "etc" / "vulkan" / "icd.d" / "MoltenVK_icd.json";

            if ( fs::exists( loaderSrc, ec ) && fs::exists( mvkSrc, ec ) && fs::exists( icdSrc, ec ) )
            {
                fs::create_directories( fwDir, ec );
                // copy_options::none on a symlink source copies the TARGET file (what we want).
                fs::copy_file( fs::canonical( loaderSrc, ec ), fwDir / "libvulkan.1.dylib",
                               fs::copy_options::overwrite_existing, ec );
                fs::copy_file( fs::canonical( mvkSrc, ec ), fwDir / "libMoltenVK.dylib",
                               fs::copy_options::overwrite_existing, ec );

                std::string icd = Common::Utils::FileSystem::ReadFileContent( icdSrc );
                const auto  keyPos = icd.find( "\"library_path\"" );
                if ( keyPos != std::string::npos )
                {
                    const auto valStart = icd.find( '\"', icd.find( ':', keyPos ) );
                    const auto valEnd   = icd.find( '\"', valStart + 1 );
                    if ( valStart != std::string::npos && valEnd != std::string::npos )
                        icd = icd.substr( 0, valStart + 1 ) + "./libMoltenVK.dylib" + icd.substr( valEnd );
                }
                Common::Utils::FileSystem::WriteContentToFile( fwDir / "MoltenVK_icd.json", icd );

                bundledVulkan = !ec;
                stats.Files += 3;
            }
            else
            {
                LOG_WARN( "[Package] Homebrew Vulkan artifacts not found under {} — the .app will fall "
                          "back to the target machine's Homebrew",
                          brew.string() );
            }
        }

        // 6) Launcher + (bundle) Info.plist. The launcher script is the bundle's CFBundleExecutable:
        // dyld reads DYLD_* only at process start, so the env MUST be set before the real binary execs.
        if ( bundle )
        {
            std::ostringstream run;
            run << "#!/usr/bin/env bash\n"
                << "# Launches " << projectName << " (packaged by the Desert Editor).\n"
                << "set -euo pipefail\n"
                << "DIR=\"$(cd \"$(dirname \"$0\")\" && pwd)\"\n"
                << "if [ -f \"$DIR/../Frameworks/MoltenVK_icd.json\" ]; then\n"
                << "  export VK_ICD_FILENAMES=\"$DIR/../Frameworks/MoltenVK_icd.json\"\n"
                << "  export DYLD_FALLBACK_LIBRARY_PATH=\"$DIR/../Frameworks${DYLD_FALLBACK_LIBRARY_PATH:+:$DYLD_FALLBACK_LIBRARY_PATH}\"\n"
                << "else\n"
                << "  BREW_PREFIX=\"${HOMEBREW_PREFIX:-$(brew --prefix 2>/dev/null || echo /opt/homebrew)}\"\n"
                << "  export VK_ICD_FILENAMES=\"${VK_ICD_FILENAMES:-$BREW_PREFIX/etc/vulkan/icd.d/MoltenVK_icd.json}\"\n"
                << "  export DYLD_FALLBACK_LIBRARY_PATH=\"$BREW_PREFIX/lib${DYLD_FALLBACK_LIBRARY_PATH:+:$DYLD_FALLBACK_LIBRARY_PATH}\"\n"
                << "fi\n"
                << "cd \"$DIR/../Resources\"\n"
                << "exec \"$DIR/Runtime-bin\" --project " << safeName << ".deproj \"$@\"\n";
            const fs::path launcher = binDir / "Runtime";
            Common::Utils::FileSystem::WriteContentToFile( launcher, run.str() );
            makeExecutable( launcher );

            std::ostringstream plist;
            plist << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                  << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                     "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                  << "<plist version=\"1.0\"><dict>\n"
                  << "  <key>CFBundleName</key><string>" << projectName << "</string>\n"
                  << "  <key>CFBundleExecutable</key><string>Runtime</string>\n"
                  << "  <key>CFBundleIdentifier</key><string>com.desertengine." << safeName << "</string>\n"
                  << "  <key>CFBundlePackageType</key><string>APPL</string>\n"
                  << "  <key>CFBundleShortVersionString</key><string>1.0</string>\n"
                  << "  <key>NSHighResolutionCapable</key><true/>\n"
                  << "</dict></plist>\n";
            Common::Utils::FileSystem::WriteContentToFile( root / "Contents" / "Info.plist", plist.str() );
        }
        else
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
            const fs::path runSh = root / "run.sh";
            Common::Utils::FileSystem::WriteContentToFile( runSh, run.str() );
            makeExecutable( runSh );
        }

        std::ostringstream msg;
        msg << "Packaged '" << projectName << "' -> " << fs::absolute( root, ec ).string() << "  ("
            << stats.Files << " files, " << ( stats.Bytes / ( 1024 * 1024 ) ) << " MB, " << options.Config
            << " runtime" << ( bundle ? ( bundledVulkan ? ", Vulkan bundled" : ", Vulkan NOT bundled" ) : "" )
            << ")";
        LOG_INFO( "[Package] {}", msg.str() );
        return { true, msg.str(), fs::absolute( root, ec ).string() };
    }
} // namespace Desert::Editor
