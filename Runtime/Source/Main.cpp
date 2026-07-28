// Desert Runtime — the standalone PLAYER. Runs a project's scene in Play mode, no editor UI.
//
//   Runtime --project <path/to/.deproj> [--scene <path/to/.desce>]
//
// The engine ships this (like Unity's player / UE's game target) — game projects do not write their
// own runner. Launch via scripts/MacOS/RunRuntime.sh.

#include <Engine/Desert.hpp>
#include <Engine/EntryPoint.hpp>
#include <Engine/Project/ProjectContext.hpp>

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

    ApplicationInfo appInfo;
    appInfo.Title = Desert::Project::ProjectContext::Current().Name;
    appInfo.VSync = true; // a game default: tear-free presentation
    // Width/Height left as std::nullopt -> fullscreen at the monitor's native resolution.

    return &Common::Singleton<Desert::Player::RuntimeApp>::CreateInstance( appInfo );
}
