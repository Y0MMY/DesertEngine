#include "GamePackager.hpp"
#include "PackageCook.hpp"
#include "PackageTarget.hpp"
#include "PackagedContentTrees.hpp"

#include <Engine/Core/ShaderCompiler/ShaderCacheKey.hpp>
#include <Engine/Project/ProjectContext.hpp>

#include <Common/Core/Constants.hpp>
#include <Common/Core/Logger.hpp>
#include <Common/Project/ProjectFormat.hpp>
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

        // Streams every file under `from` into the pak as "<keyPrefix>/<relative>", skipping raw mesh
        // sources when asked. It replaced a loose-file `CopyTree` with the same filter semantics, and
        // that function then sat here uncalled until `-Wunused-function` was allowed to say so.
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

        // THE TARGET IS THIS EDITOR'S OWN HOST, and everything below that used to be a macOS literal now
        // comes out of that one description (PackageTarget.hpp): the binary's name, whether a .app is a
        // thing at all, the launcher, and the build script named in the error. The panel reads the same
        // description to say which platform it can produce, so the two cannot disagree — which is the
        // repair П6 was opened for.
        const TargetPlatformInfo& host = HostPlatformInfo();

        // 1) The Runtime binary for the chosen configuration (editor cwd is Editor/). The FILE NAME is
        // the host's: looking for an extensionless `Runtime` on Windows could only ever fail, and it
        // failed by naming a macOS build script in the message.
        const fs::path  runtimeBin = fs::path( ".." ) / "build" / "Bin" / options.Config / host.RuntimeBinary;
        std::error_code ec;
        if ( !fs::exists( runtimeBin, ec ) )
            return { false,
                     "Runtime binary not found (" + runtimeBin.string() +
                          "). Build it first: " + host.BuildScript + " " + options.Config,
                     "" };

        // Layout: a macOS .app bundle (default there) or a plain folder. Same content either way — the
        // pak keys are mount-root-relative, so whatever directory holds Content.dpak becomes the content
        // root the VFS serves from.
        //
        // A .app asked for on a host that has no such concept is REFUSED OUT LOUD rather than obeyed or
        // dropped: obeying it produced a bundle-shaped directory with a bash launcher inside on Windows,
        // and dropping it quietly is the silent substitution §1.4 forbids.
        bool bundle = options.MacAppBundle;
        if ( bundle && !host.SupportsAppBundle )
        {
            LOG_WARN( "[Package] a .app bundle was requested but {} has no such layout — packaging as a "
                      "plain folder with {}",
                      host.DisplayName, host.LauncherName );
            bundle = false;
        }

        const fs::path root    = fs::path( options.OutputDir ) / ( bundle ? safeName + ".app" : safeName );
        const fs::path binDir  = bundle ? root / "Contents" / "MacOS" : root;
        const fs::path resDir  = bundle ? root / "Contents" / "Resources" : root;
        const fs::path fwDir   = root / "Contents" / "Frameworks"; // bundle only
        const char*    binName = bundle ? "Runtime-bin" : host.RuntimeBinary;

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

        // 3) Cook BEFORE packing: every deterministic startup cost — shader SPIR-V, font atlases,
        // icon SDFs — is paid here, once, into the project's Cooked/ tree, so the census below ships
        // the artifacts and the player's first launch reads instead of rebuilding. Cooked for the
        // TARGET runtime's profile (options.Config), not this editor's: a Debug editor packaging a
        // Release game must produce Release cache keys or the shipped cache never hits.
        const CookStats cook = CookContentCaches( Core::SpirvDebugInfoForConfigName( options.Config ) );

        // ALL content goes into ONE Content.dpak (UE .pak model), tree by tree out of the shared
        // census (PackagedContentTrees.hpp) — assets, cooked cache, shaders, fonts, icons. The Runtime
        // mounts the archive at startup; every content read resolves through the VFS.
        {
            Common::Utils::PakWriter pak( resDir / "Content.dpak" );
            if ( !pak.IsOpen() )
                return { false, "Cannot create Content.dpak in " + resDir.string(), "" };

            for ( const PackagedTree& tree : PackagedContentTrees() )
                if ( !AddTreeToPak( pak, *tree.Tree, tree.PakKey, tree.StripRawMeshSources, stats, error ) )
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
                defaultScene = kPackagedAssetsRoot + defaultScene.substr( oldRoot.size() );

            // Through the shared serializer (ProjectFormat.hpp) — this used to be the fourth
            // hand-spliced copy of the .deproj format, and a project name with a quote in it
            // shipped a package the Runtime could not open.
            Common::Project::ProjectFile deproj;
            deproj.Name         = projectName;
            deproj.AssetsRoot   = kPackagedAssetsRoot;
            deproj.DefaultScene = defaultScene;
            // FAILS THE PACKAGE, like every other step in this function. The .deproj is the file the
            // Runtime is handed on the command line by the launcher below; without it the shipped
            // build starts, finds no project and exits. Package() already refuses on a missing pak, a
            // bad tree and a failed finalize — these five writes were the only steps outside that.
            const fs::path deprojPath = resDir / ( safeName + ".deproj" );
            if ( const auto written = Common::Utils::FileSystem::WriteContentToFileAtomic(
                      deprojPath, Common::Project::WriteProjectFile( deproj ) );
                 !written )
                return { false, "Could not write " + deprojPath.string() + ": " + written.GetError(), "" };
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

                // Guarded by fs::exists(icdSrc) above; an unreadable file degrades to the same
                // "library_path key not found" no-op patch the old empty read produced.
                auto        icdRead = Common::Utils::FileSystem::ReadFileContent( icdSrc );
                std::string icd     = icdRead ? icdRead.ExtractValue() : std::string{};
                const auto  keyPos = icd.find( "\"library_path\"" );
                if ( keyPos != std::string::npos )
                {
                    const auto valStart = icd.find( '\"', icd.find( ':', keyPos ) );
                    const auto valEnd   = icd.find( '\"', valStart + 1 );
                    if ( valStart != std::string::npos && valEnd != std::string::npos )
                        icd = icd.substr( 0, valStart + 1 ) + "./libMoltenVK.dylib" + icd.substr( valEnd );
                }
                const auto icdWritten =
                     Common::Utils::FileSystem::WriteContentToFileAtomic( fwDir / "MoltenVK_icd.json", icd );
                if ( !icdWritten )
                    LOG_WARN( "[Package] MoltenVK_icd.json was not written: {} — the .app falls back to "
                              "the target machine's Homebrew Vulkan",
                              icdWritten.GetError() );

                // Not fatal, and now honest about it: the bundle only CLAIMS to carry Vulkan when the
                // ICD that points at the bundled dylib is really there. It used to claim it whenever
                // the copies succeeded, so a package with an unwritten ICD reported "Vulkan bundled"
                // and then failed to find a driver on a machine without Homebrew.
                bundledVulkan = !ec && icdWritten.IsSuccess();
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
            // This script IS the bundle's CFBundleExecutable — without it macOS reports the app as
            // damaged, which is the least diagnosable failure in this whole function.
            if ( const auto written = Common::Utils::FileSystem::WriteContentToFileAtomic( launcher, run.str() );
                 !written )
                return { false, "Could not write the launcher " + launcher.string() + ": " + written.GetError(),
                         "" };
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
            const fs::path plistPath = root / "Contents" / "Info.plist";
            if ( const auto written =
                      Common::Utils::FileSystem::WriteContentToFileAtomic( plistPath, plist.str() );
                 !written )
                return { false, "Could not write " + plistPath.string() + ": " + written.GetError(), "" };
        }
        else
        {
            // The plain-folder launcher, in the host's own shell. On macOS it has to find MoltenVK
            // through Homebrew (there is no Frameworks directory outside a bundle); on Windows the
            // Vulkan loader is installed by the graphics driver and there is nothing to point at, so the
            // script only has to cd and run. Writing the bash version on Windows produced a `run.sh`
            // nothing there can execute.
            std::ostringstream run;
            if ( host.Platform == TargetPlatform::Windows )
            {
                run << "@echo off\r\n"
                    << "REM Launches " << projectName << " (packaged by the Desert Editor).\r\n"
                    << "cd /d \"%~dp0\"\r\n"
                    << "\"" << host.RuntimeBinary << "\" --project " << safeName << ".deproj %*\r\n";
            }
            else
            {
                run << "#!/usr/bin/env bash\n"
                    << "# Launches " << projectName << " (packaged by the Desert Editor).\n"
                    << "set -euo pipefail\n"
                    << "cd \"$(dirname \"$0\")\"\n"
                    << "BREW_PREFIX=\"${HOMEBREW_PREFIX:-$(brew --prefix 2>/dev/null || echo /opt/homebrew)}\"\n"
                    << "export "
                       "VK_ICD_FILENAMES=\"${VK_ICD_FILENAMES:-$BREW_PREFIX/etc/vulkan/icd.d/"
                       "MoltenVK_icd.json}\"\n"
                    << "export "
                       "DYLD_FALLBACK_LIBRARY_PATH=\"$BREW_PREFIX/"
                       "lib${DYLD_FALLBACK_LIBRARY_PATH:+:$DYLD_FALLBACK_LIBRARY_PATH}\"\n"
                    << "exec ./" << host.RuntimeBinary << " --project " << safeName << ".deproj \"$@\"\n";
            }
            const fs::path launcher = root / host.LauncherName;
            if ( const auto written = Common::Utils::FileSystem::WriteContentToFileAtomic( launcher, run.str() );
                 !written )
                return { false, "Could not write " + launcher.string() + ": " + written.GetError(), "" };
            makeExecutable( launcher );
        }

        std::ostringstream msg;
        msg << "Packaged '" << projectName << "' -> " << fs::absolute( root, ec ).string() << "  ("
            << stats.Files << " files, " << ( stats.Bytes / ( 1024 * 1024 ) ) << " MB, " << options.Config
            << " runtime" << ( bundle ? ( bundledVulkan ? ", Vulkan bundled" : ", Vulkan NOT bundled" ) : "" )
            << ")";
        // An artifact the cook could not write is a hole in the shipped cache that nothing downstream
        // can notice — the pak packs whatever is there and the game starts, just slowly, on the
        // player's machine. So it is said HERE, in the result the packaging UI shows, and not left to
        // a log line nobody reads. (Compile/bake failures are NOT raised this way: a project may ship
        // a deliberately broken shader, and the runtime reports that one for itself.)
        if ( cook.StoreFailures > 0 )
            msg << "  WARNING: " << cook.StoreFailures
                << " cooked artifact(s) could not be written; the game will rebuild them at every start";
        LOG_INFO( "[Package] {}", msg.str() );
        return { true, msg.str(), fs::absolute( root, ec ).string() };
    }
    PackageResult BuildContentPak()
    {
        using Project::ProjectContext;
        if ( !ProjectContext::HasProject() )
            return { false, "No project is open.", "" };

        std::error_code ec;
        const fs::path pakPath = fs::path( ProjectContext::Directory() ) / "Content.dpak";

        CopyStats   stats;
        std::string error;

        Common::Utils::PakWriter pak( pakPath );
        if ( !pak.IsOpen() )
            return { false, "Cannot create " + pakPath.string(), "" };

        // Same cook as PackageGame, for THIS build's profile: the dev pak serves the runtime the
        // developer launches next to this editor, which is built in the same configuration. (A
        // cross-config dev runtime misses and self-heals into loose Cooked/ — dev machines are
        // writable; only the shipped package must never rely on that.)
        const CookStats cook = CookContentCaches( Core::SpirvDebugInfoThisBuild() );

        // The same census PackageGame packs — one list, two entry points (see PackagedContentTrees.hpp).
        for ( const PackagedTree& tree : PackagedContentTrees() )
            if ( !AddTreeToPak( pak, *tree.Tree, tree.PakKey, tree.StripRawMeshSources, stats, error ) )
                return { false, error, "" };

        if ( pak.Finalize() == 0 )
            return { false, "Failed to finalize " + pakPath.string() + " (no entries?)", "" };

        std::ostringstream msg;
        msg << "Content.dpak rebuilt: " << stats.Files << " file(s), " << ( stats.Bytes / ( 1024 * 1024 ) )
            << " MB -> " << fs::absolute( pakPath, ec ).string();
        if ( cook.StoreFailures > 0 )
            msg << "  WARNING: " << cook.StoreFailures
                << " cooked artifact(s) could not be written; the game will rebuild them at every start";
        LOG_INFO( "[Package] {}", msg.str() );
        return { true, msg.str(), fs::absolute( pakPath, ec ).string() };
    }
} // namespace Desert::Editor
