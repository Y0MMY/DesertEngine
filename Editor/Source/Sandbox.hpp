#pragma once

#include <Engine/Desert.hpp>
#include <Engine/EntryPoint.hpp>

#include <Editor/Core/ProjectContext.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Desert
{
    class Sandbox : public Engine::Application
    {
    public:
        Sandbox( const Engine::ApplicationInfo& appinfo );

        virtual void OnCreate() override;
        virtual void OnDestroy() override;
    };
} // namespace Desert

Desert::Engine::Application* CreateApplication( int argc, char** argv )
{
    using namespace Desert::Engine;

    // The editor is PROJECT-DRIVEN: `--project <path/to/.deproj>` is REQUIRED. Picking/creating projects
    // is the Project Hub's job (Tools/ProjectHub, scripts/MacOS/RunProjectHub.sh) — the editor itself
    // never shows a chooser. Opening the project also remaps every engine content path into the project
    // folder, so it must happen BEFORE anything engine-side spins up.
    for ( int i = 1; i < argc - 1; ++i )
        if ( std::strcmp( argv[i], "--project" ) == 0 )
        {
            if ( !Desert::Editor::ProjectContext::Open( argv[i + 1] ) )
            {
                std::fprintf( stderr, "Could not open project '%s' (missing or corrupt .deproj).\n",
                              argv[i + 1] );
                std::exit( 1 );
            }
        }

    if ( !Desert::Editor::ProjectContext::HasProject() )
    {
        std::fprintf( stderr,
                      "No project given. Start the editor through the Project Hub\n"
                      "(scripts/MacOS/RunProjectHub.sh) or pass: --project <path/to/.deproj>\n" );
        std::exit( 1 );
    }

    ApplicationInfo appInfo;
    appInfo.Title = "Desert Engine — " + Desert::Editor::ProjectContext::Current().Name;
    appInfo.VSync = false;
    // Width/Height left as std::nullopt -> start fullscreen at the monitor's native resolution.

    return &Common::Singleton<Desert::Sandbox>::CreateInstance( appInfo );
}
