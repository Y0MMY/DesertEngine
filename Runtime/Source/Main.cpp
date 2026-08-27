// Desert Runtime — the standalone PLAYER. Runs a project's scene in Play mode, no editor UI.
//
// PACKAGED (UE-style, zero-config): double-click the exe. It mounts the archive named after itself next
// to it — MyGame.exe -> MyGame.dpak (else Content.dpak) — and opens the project descriptor Game.deproj
// from the archive root. The scene is the project's DefaultScene. Ship a folder of just: exe + one .dpak.
//
// DEV: pass --project <path/to/.deproj> [--scene <path/to/.desce>] to run a loose on-disk project; these
// override the packaged discovery. Launch via scripts/MacOS/RunRuntime.sh.

#include <Engine/Desert.hpp>
#include <Engine/EntryPoint.hpp>
#include <Engine/Project/ProjectContext.hpp>

#include <Common/Utilities/VFS.hpp>
#include <Common/Utilities/FileSystem.hpp>
#include <Common/Core/Version.hpp>

#include <algorithm>
#include <filesystem>
#include <vector>

#include "RuntimeLayer.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace Desert::Player
{
    static std::string s_SceneOverride;

    class RuntimeApp : public Engine::Application
    {
    public:
        explicit RuntimeApp( const Engine::ApplicationInfo& appinfo ) : Engine::Application( appinfo )
        {
        }

        void OnCreate() override
        {
            PushLayer( new RuntimeLayer( s_SceneOverride ) );
        }

        void OnDestroy() override
        {
        }
    };
} // namespace Desert::Player

// Pick the packaged archive next to the executable. Preference order (deterministic):
//   1. <exe-basename>.dpak   (UE-style: rename exe + pak together = a different game)
//   2. Content.dpak          (the default packaging name)
//   3. the ONLY *.dpak in the folder, if exactly one exists (survives a rename of the pak alone)
// Patch*.dpak are excluded here (they mount on top afterwards). Returns empty if none / ambiguous.
static std::filesystem::path FindBasePak( const std::filesystem::path& dir, const std::string& exeStem )
{
    namespace fs         = std::filesystem;
    const fs::path named = dir / ( exeStem + ".dpak" );
    if ( fs::exists( named ) )
        return named;
    const fs::path content = dir / "Content.dpak";
    if ( fs::exists( content ) )
        return content;

    std::vector<fs::path> candidates;
    std::error_code       ec;
    for ( const auto& de : fs::directory_iterator( dir, ec ) )
        if ( de.is_regular_file( ec ) && de.path().extension() == ".dpak" &&
             de.path().filename().string().rfind( "Patch", 0 ) != 0 )
            candidates.push_back( de.path() );
    if ( candidates.size() == 1 )
        return candidates.front();
    if ( candidates.size() > 1 )
        std::fprintf( stderr,
                      "[Runtime] %zu archives next to the exe and none named '%s.dpak' or "
                      "'Content.dpak' — cannot pick one. Rename the game archive to match the exe.\n",
                      candidates.size(), exeStem.c_str() );
    return {};
}

std::unique_ptr<Desert::Engine::Application> CreateApplication( int argc, char** argv )
{
    using namespace Desert::Engine;
    namespace fs = std::filesystem;

    std::string projectArg;
    for ( int i = 1; i + 1 < argc; ++i )
    {
        if ( std::strcmp( argv[i], "--project" ) == 0 )
            projectArg = argv[++i];
        else if ( std::strcmp( argv[i], "--scene" ) == 0 )
            Desert::Player::s_SceneOverride = argv[++i];
    }

    // DEV: an explicit --project opens the loose on-disk descriptor (overrides packaged discovery).
    if ( !projectArg.empty() && !Desert::Project::ProjectContext::Open( projectArg ) )
    {
        std::fprintf( stderr, "Could not open project '%s' (missing or corrupt .deproj).\n", projectArg.c_str() );
        std::exit( 1 );
    }

    // Content directory: the project's folder (dev) or the executable's own folder (packaged).
    const fs::path exePath = Common::Utils::FileSystem::ExecutablePath();
    const fs::path baseDir = Desert::Project::ProjectContext::HasProject()
                                  ? fs::path( Desert::Project::ProjectContext::Directory() )
                                  : ( exePath.empty() ? fs::current_path() : exePath.parent_path() );

    // Mount the base archive (skipped in dev if there is none — reads stay plain disk reads), then any
    // Patch*.dpak ON TOP in name order (later overrides earlier), so shipping a fix = dropping one pak.
    {
        const fs::path pak = FindBasePak( baseDir, exePath.stem().string() );
        if ( !pak.empty() )
            Common::Utils::VFS::MountPak( pak );

        std::vector<fs::path> patches;
        std::error_code       ec;
        for ( const auto& de : fs::directory_iterator( baseDir, ec ) )
            if ( de.is_regular_file( ec ) && de.path().extension() == ".dpak" &&
                 de.path().filename().string().rfind( "Patch", 0 ) == 0 )
                patches.push_back( de.path() );
        std::sort( patches.begin(), patches.end() );
        for ( const auto& p : patches )
            Common::Utils::VFS::MountPak( p );
    }

    // PACKAGED: the descriptor lives at the archive root as Game.deproj — open it through the now-mounted VFS.
    if ( !Desert::Project::ProjectContext::HasProject() )
        Desert::Project::ProjectContext::Open( ( baseDir / "Game.deproj" ).string() );

    if ( !Desert::Project::ProjectContext::HasProject() )
    {
        std::fprintf( stderr,
                      "No game to run.\n"
                      "  Packaged: put '%s.dpak' (or 'Content.dpak') containing a 'Game.deproj' next to the "
                      "executable.\n"
                      "  Dev:      pass --project <path/to/.deproj> [--scene <path/to/.desce>].\n",
                      exePath.stem().string().c_str() );
        std::exit( 1 );
    }

    std::printf( "Desert Runtime %s — %s\n", Common::Version::Full(),
                 Desert::Project::ProjectContext::Current().Name.c_str() );

    ApplicationInfo appInfo;
    appInfo.Title = Desert::Project::ProjectContext::Current().Name;
    appInfo.VSync = true; // a game default: tear-free presentation
    // Width/Height left as std::nullopt -> fullscreen at the monitor's native resolution.

    return std::make_unique<Desert::Player::RuntimeApp>( appInfo );
}
