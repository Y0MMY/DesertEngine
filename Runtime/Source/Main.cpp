// Desert Runtime — the standalone PLAYER. Runs a project's scene in Play mode, no editor UI.
//
//   Runtime --project <path/to/.deproj> [--scene <path/to/.desce>]
//
// The engine ships this (like Unity's player / UE's game target) — game projects do not write their
// own runner. Launch via scripts/MacOS/RunRuntime.sh.

#include <Engine/Desert.hpp>
#include <Engine/EntryPoint.hpp>
#include <Engine/Project/ProjectContext.hpp>

#include <Common/Utilities/VFS.hpp>
#include <Common/Core/Version.hpp>

#include <filesystem>

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

Desert::Engine::Application* CreateApplication( int argc, char** argv )
{
    using namespace Desert::Engine;

    for ( int i = 1; i < argc - 1; ++i )
    {
        if ( std::strcmp( argv[i], "--project" ) == 0 )
        {
            if ( !Desert::Project::ProjectContext::Open( argv[i + 1] ) )
            {
                std::fprintf( stderr, "Could not open project '%s' (missing or corrupt .deproj).\n",
                              argv[i + 1] );
                std::exit( 1 );
            }
        }
        else if ( std::strcmp( argv[i], "--scene" ) == 0 )
        {
            Desert::Player::s_SceneOverride = argv[i + 1];
        }
    }

    if ( !Desert::Project::ProjectContext::HasProject() )
    {
        std::fprintf( stderr, "Usage: Runtime --project <path/to/.deproj> [--scene <path/to/.desce>]\n" );
        std::exit( 1 );
    }

    // Packaged game: content lives in Content.dpak next to the .deproj — mount it so every engine
    // read (scenes, cooked meshes, textures, shaders, scripts) resolves through the VFS. In a dev
    // project the archive simply does not exist and reads stay plain disk reads.
    // UPDATES: any Patch*.dpak next to it mounts ON TOP in name order (Patch_001, Patch_002, ...) —
    // later mounts override earlier keys, so shipping a fix = dropping one small pak beside the base.
    {
        const auto dir = std::filesystem::path( Desert::Project::ProjectContext::Directory() );
        const auto pak = dir / "Content.dpak";
        if ( std::filesystem::exists( pak ) )
            Common::Utils::VFS::MountPak( pak );

        std::vector<std::filesystem::path> patches;
        std::error_code                    ec;
        for ( const auto& de : std::filesystem::directory_iterator( dir, ec ) )
            if ( de.is_regular_file( ec ) && de.path().extension() == ".dpak" &&
                 de.path().filename().string().rfind( "Patch", 0 ) == 0 )
                patches.push_back( de.path() );
        std::sort( patches.begin(), patches.end() );
        for ( const auto& p : patches )
            Common::Utils::VFS::MountPak( p );
    }

    std::printf( "Desert Runtime %s\n", Common::Version::Full() );

    ApplicationInfo appInfo;
    appInfo.Title = Desert::Project::ProjectContext::Current().Name;
    appInfo.VSync = true; // a game default: tear-free presentation
    // Width/Height left as std::nullopt -> fullscreen at the monitor's native resolution.

    return &Common::Singleton<Desert::Player::RuntimeApp>::CreateInstance( appInfo );
}
