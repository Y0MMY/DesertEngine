#pragma once

#include <Engine/Desert.hpp>
#include <Engine/EntryPoint.hpp>

#include <Editor/Core/ProjectContext.hpp>
#include <Editor/Core/ShotOptions.hpp>

#include <Common/Core/Profiler.hpp>

#include <algorithm>
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

    // Screenshot mode (see Editor/Core/ShotOptions.hpp). Parsed here rather than in the layer because
    // --scene has to be known before the layer decides what to load, and because a headful editor and a
    // one-shot capture differ in nothing else: same renderer, same passes, same frame.
    {
        auto& shot = Desert::Editor::ShotOptions::Get();

        auto readVec3 = []( const char* text, glm::vec3& out )
        {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            if ( std::sscanf( text, "%f,%f,%f", &x, &y, &z ) != 3 )
                return false;
            out = glm::vec3( x, y, z );
            return true;
        };

        for ( int i = 1; i < argc; ++i )
        {
            const bool hasNext = i + 1 < argc;
            if ( hasNext && std::strcmp( argv[i], "--scene" ) == 0 )
                shot.Scene = argv[++i];
            else if ( hasNext && std::strcmp( argv[i], "--shot" ) == 0 )
                shot.Output = argv[++i];
            else if ( hasNext && std::strcmp( argv[i], "--shot-frames" ) == 0 )
                shot.Frames = std::atoi( argv[++i] );
            else if ( hasNext && std::strcmp( argv[i], "--camera" ) == 0 )
                shot.HasCamera = readVec3( argv[++i], shot.Position ) || shot.HasCamera;
            else if ( hasNext && std::strcmp( argv[i], "--look" ) == 0 )
                shot.HasCamera = readVec3( argv[++i], shot.Forward ) || shot.HasCamera;
            // The far end of a moving shot. Each also implies --camera, because a path that nothing
            // places is a path the scene's own camera ignores.
            else if ( hasNext && std::strcmp( argv[i], "--camera-to" ) == 0 )
            {
                shot.HasPositionTo = readVec3( argv[++i], shot.PositionTo ) || shot.HasPositionTo;
                shot.HasCamera     = shot.HasCamera || shot.HasPositionTo;
            }
            else if ( hasNext && std::strcmp( argv[i], "--look-to" ) == 0 )
            {
                shot.HasForwardTo = readVec3( argv[++i], shot.ForwardTo ) || shot.HasForwardTo;
                shot.HasCamera    = shot.HasCamera || shot.HasForwardTo;
            }
            else if ( hasNext && std::strcmp( argv[i], "--shot-sequence" ) == 0 )
                shot.Sequence = argv[++i];
            else if ( hasNext && std::strcmp( argv[i], "--shot-every" ) == 0 )
                shot.SequenceEvery = std::max( 1, std::atoi( argv[++i] ) );
            // No argument, so it is checked against argc rather than hasNext — as the last flag on the
            // line it would otherwise be silently dropped.
            else if ( std::strcmp( argv[i], "--gpu-profile" ) == 0 )
                shot.GpuProfile = true;
            else if ( std::strcmp( argv[i], "--no-gpu-timing" ) == 0 )
                shot.GpuTiming = false;
            else if ( std::strcmp( argv[i], "--gpu-profile-frame-only" ) == 0 )
                shot.GpuFrameOnly = true;
        }

        // Applied here, before the renderer exists: the flags have to be in force for the very first
        // frame, or a measurement would include a few frames of the other configuration.
        //
        // GPU timing is OFF unless --gpu-profile asks for it. A run that did not ask to be measured is
        // not measured, and its frame time is the one a budget decision should be taken on.
        Common::Profiling::Profiler::Get().GpuEnabled()    = shot.GpuProfile && shot.GpuTiming;
        Common::Profiling::Profiler::Get().GpuPassScopes() = !shot.GpuFrameOnly;
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
