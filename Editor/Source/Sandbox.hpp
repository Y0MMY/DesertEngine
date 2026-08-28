#pragma once

#include <Engine/Desert.hpp>
#include <Engine/EntryPoint.hpp>

#include <Editor/Core/CommandLine.hpp>
#include <Editor/Core/ProjectContext.hpp>
#include <Editor/Core/ShotOptions.hpp>
#include <Editor/Core/StartupOptions.hpp>

#include <Common/Core/Profiler.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

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

std::unique_ptr<Desert::Engine::Application> CreateApplication( int argc, char** argv )
{
    using namespace Desert::Engine;

    // THE WHOLE COMMAND LINE, IN ONE PASS, AND NOTHING FALLS OFF THE END OF IT.
    //
    // This used to be two loops of if/else here — one for `--project`, one for everything else — and both
    // shared a defect the chain's shape makes almost invisible: a token that matched no branch was simply
    // DROPPED. `--sceen` (one transposed letter) parsed to nothing, so the editor loaded the project's
    // default scene, rendered it, wrote a plausible PNG under the name the caller had chosen and exited 0.
    // Measured on the binary before this change: the log read "Loading scene: Desert Sandbox" and the flag
    // was never mentioned. The same silence swallowed a value-taking flag written last, a `--camera` with
    // two components instead of three, and a `--shot-frames` that was not a number.
    //
    // The parse is now a PURE function (Editor/Core/CommandLine.hpp) so the decision can be asserted by a
    // test instead of discovered by launching the editor and looking at what came out. What is left here
    // is only what genuinely needs the process: opening the project, and publishing the result into the
    // two singletons the rest of the editor reads.
    std::vector<std::string> args;
    args.reserve( static_cast<std::size_t>( argc > 1 ? argc - 1 : 0 ) );
    for ( int i = 1; i < argc; ++i )
        args.emplace_back( argv[i] );

    auto parsed = Desert::Editor::ParseCommandLine( args );
    if ( !parsed.IsSuccess() )
    {
        // stderr and a non-zero status, before any engine subsystem exists. There is no logger yet and no
        // frame to spoil, and a caller that reads the exit code learns the truth on the first byte.
        std::fprintf( stderr, "%s\n", parsed.GetError().c_str() );
        std::exit( 2 );
    }

    const Desert::Editor::CommandLineOptions options = parsed.ExtractValue();

    // The editor is PROJECT-DRIVEN: `--project <path/to/.deproj>` is REQUIRED. Picking/creating projects
    // is the Project Hub's job (Tools/ProjectHub, scripts/MacOS/RunProjectHub.sh) — the editor itself
    // never shows a chooser. Opening the project also remaps every engine content path into the project
    // folder, so it must happen BEFORE anything engine-side spins up.
    if ( !options.Project.empty() )
    {
        if ( !Desert::Editor::ProjectContext::Open( options.Project ) )
        {
            std::fprintf( stderr, "Could not open project '%s' (missing or corrupt .deproj).\n",
                          options.Project.c_str() );
            std::exit( 1 );
        }
    }

    // Published before the renderer exists: the flags have to be in force for the very first frame, or a
    // measurement would include a few frames of the other configuration.
    Desert::Editor::ShotOptions::Get()    = options.Shot;
    Desert::Editor::StartupOptions::Get() = options.Startup;

    // GPU timing is OFF unless --gpu-profile asks for it. A run that did not ask to be measured is not
    // measured, and its frame time is the one a budget decision should be taken on.
    Common::Profiling::Profiler::Get().GpuEnabled()    = options.Shot.GpuProfile && options.Shot.GpuTiming;
    Common::Profiling::Profiler::Get().GpuPassScopes() = !options.Shot.GpuFrameOnly;

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

    return std::make_unique<Desert::Sandbox>( appInfo );
}
