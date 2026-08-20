#pragma once

#include <Common/Core/JobSystem.hpp>
#include <Engine/Core/EngineContext.hpp>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <typeinfo>

// `execinfo.h` is POSIX and does not exist under MSVC. `_WIN32` rather than the project's own
// DESERT_PLATFORM_WINDOWS: this is a question about the toolchain's headers, and the compiler is the
// authority on that whether or not the build system remembered to say so.
#ifndef _WIN32
#include <execinfo.h>
#include <pthread.h>
#endif

extern Desert::Engine::Application* CreateApplication( int argc, char** argv );

/**
 * @brief Prints the exception and the THROWING stack when an exception escapes to std::terminate.
 *
 * WHY THIS EXISTS. An uncaught exception in this engine used to produce exactly one line —
 * `libc++abi: terminating due to uncaught exception of type ...` — and nothing else: macOS writes no
 * crash report for an abort whose signature it has already seen, and a defect that appears in one run
 * in fifty does not survive being run under a debugger. A rare abort in the volumetric cloud phase cost
 * a whole task for precisely that reason and was handed on unsolved (Docs/Clouds/CALIBRATION.md §A0).
 * Thirty lines here would have named it in one run.
 *
 * WHY THE STACK IS STILL THERE. For an UNCAUGHT exception libc++abi calls std::terminate from the
 * point of the throw, without unwinding first — the standard permits either and libc++ chooses this
 * one — so the frames that threw are live and `backtrace()` walks them.
 *
 * `_Exit` rather than `abort`: the frames have been printed, and abort would run the same teardown that
 * is already known to fault on the way out, burying the message that was the point.
 */
inline void DesertTerminateBacktrace()
{
#ifndef _WIN32
    void*     frames[128];
    const int count = backtrace( frames, 128 );
    std::fprintf( stderr, "\n=== DESERT TERMINATE (thread %p) ===\n", (void*)pthread_self() );
#else
    std::fprintf( stderr, "\n=== DESERT TERMINATE ===\n" );
#endif

    // The exception itself is worth printing on EVERY platform even where the stack is not available:
    // `what()` is what says `vector` rather than merely `std::length_error`, and that one word is what
    // pointed at a torn container rather than at a bad argument.
    if ( auto ptr = std::current_exception() )
    {
        try
        {
            std::rethrow_exception( ptr );
        }
        catch ( const std::exception& e )
        {
            std::fprintf( stderr, "uncaught %s: %s\n", typeid( e ).name(), e.what() );
        }
        catch ( ... )
        {
            std::fprintf( stderr, "uncaught non-std exception\n" );
        }
    }
    std::fflush( stderr );

#ifndef _WIN32
    backtrace_symbols_fd( frames, count, 2 );
#endif

    std::fprintf( stderr, "=== END DESERT TERMINATE ===\n" );
    std::fflush( stderr );
    std::_Exit( 134 );
}

int main( int argc, char** argv )
{
    // Before anything can throw, and before the logger exists: the handler writes to stderr directly so
    // that it still works when the failure is the logger's own.
    std::set_terminate( &DesertTerminateBacktrace );

    Common::Logger::LogInit();

    auto app = CreateApplication( argc, argv );
    app->OnCreate();
    app->Run();
    app->OnDestroy();

    // Ordered teardown BEFORE static destructors run (their cross-TU order is undefined):
    // 1) join the worker pool while the logger/engine objects its jobs touch are still alive;
    // 2) drain the GPU so descriptor pools/buffers destroyed during static teardown are no longer
    //    referenced by in-flight command buffers (the exit-time VUID-vkDestroyDescriptorPool-00303
    //    followed by worker threads aborting on destroyed mutexes).
    Common::JobSystem::Get().Shutdown();
    if ( const auto device = Desert::EngineContext::GetInstance().GetDevice() )
        device->WaitIdle();

    return 0;
}