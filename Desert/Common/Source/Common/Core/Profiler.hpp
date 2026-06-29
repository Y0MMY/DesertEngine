#pragma once

// DesertEngine CPU profiler.
//
// Two layers behind one set of macros:
//   1. Optick — every scope also emits an OPTICK_EVENT, so the external Optick GUI (connect over the
//      socket server) gets the full timeline. Frame boundaries flip Optick's frame.
//   2. A lightweight in-engine aggregator (this file) — accumulates named scopes per frame so the editor
//      can show a readable "Profiler" panel and dump timings to the log WITHOUT the external GUI. This is
//      what we use to localize FPS hotspots directly.
//
// Use DESERT_PROFILE_FRAME(name) once at the top of the frame loop, and DESERT_PROFILE_SCOPE(name) inside
// any scope you want timed. Names must be string literals (so Optick can cache its descriptors).

#include <optick.h>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Common::Profiling
{
    struct ScopeResult
    {
        std::string Name;
        double      TotalMs = 0.0; // summed across all calls this frame
        uint32_t    Calls   = 0;
    };

    // Main-thread oriented CPU scope aggregator. Thread-safe (cheap mutex) so worker-thread scopes don't
    // corrupt the map, but it's designed around the render/update thread.
    class Profiler
    {
    public:
        static Profiler& Get();

        // Per-frame boundary. Accumulates scope times across all frames in the current averaging window;
        // when the window (AvgWindowSeconds) elapses it publishes the AVERAGE per-frame time of each scope.
        // Averaging makes the numbers readable at high FPS (per-frame values flicker too fast to read).
        void BeginFrame();

        void AddSample( const char* name, double ms );

        // Published, averaged-over-the-window results (see BeginFrame).
        const std::vector<ScopeResult>& LastFrame() const
        {
            return m_Display;
        }
        double LastFrameMs() const
        {
            return m_DisplayFrameMs;
        }

        bool& Enabled()
        {
            return m_Enabled;
        }
        // When true the published list is sorted by time (desc) — handy to spot hotspots, but rows jump
        // around frame-to-frame. When false it's sorted by NAME (stable order, easy to read a single row).
        bool& SortByTime()
        {
            return m_SortByTime;
        }
        // Averaging window in seconds: values are averaged over this period and refreshed once per period.
        float& AvgWindowSeconds()
        {
            return m_AvgWindowSec;
        }

    private:
        using Clock = std::chrono::high_resolution_clock;

        std::mutex                                   m_Mutex;
        std::unordered_map<std::string, ScopeResult> m_Accum; // summed over the current window (not per-frame)
        std::vector<ScopeResult>                     m_Display;
        double                                       m_DisplayFrameMs = 0.0;
        Clock::time_point                            m_WindowStart;
        uint32_t                                     m_WindowFrames  = 0;
        bool                                         m_WindowStarted = false;
        bool                                         m_Enabled       = true;
        bool                                         m_SortByTime    = true;
        float                                        m_AvgWindowSec  = 0.5f;
    };

    class ScopedTimer
    {
    public:
        explicit ScopedTimer( const char* name )
            : m_Name( name ), m_Start( std::chrono::high_resolution_clock::now() )
        {
        }
        ~ScopedTimer()
        {
            const auto end = std::chrono::high_resolution_clock::now();
            Profiler::Get().AddSample(
                 m_Name, std::chrono::duration<double, std::milli>( end - m_Start ).count() );
        }

    private:
        const char*                                    m_Name;
        std::chrono::high_resolution_clock::time_point m_Start;
    };
} // namespace Common::Profiling

#define DESERT_PROF_CONCAT_( a, b ) a##b
#define DESERT_PROF_CONCAT( a, b ) DESERT_PROF_CONCAT_( a, b )

// Time a scope: feeds BOTH Optick (external GUI) and the in-engine aggregator (editor panel + logs).
#define DESERT_PROFILE_SCOPE( NAME )                                                                        \
    OPTICK_EVENT( NAME );                                                                                   \
    ::Common::Profiling::ScopedTimer DESERT_PROF_CONCAT( _desertProf_, __LINE__ )( NAME )

// Like DESERT_PROFILE_SCOPE but for a runtime name (e.g. a render-pass name). The const char* must stay
// valid for the duration of the scope.
#define DESERT_PROFILE_SCOPE_DYNAMIC( CSTR )                                                                \
    OPTICK_EVENT_DYNAMIC( CSTR );                                                                           \
    ::Common::Profiling::ScopedTimer DESERT_PROF_CONCAT( _desertProfDyn_, __LINE__ )( CSTR )

// Times the enclosing function, named automatically from the function name — drop one line at the top of
// any method you want to track (don't blanket EVERY tiny getter: the clock read + map insert per call adds
// up and drowns the signal — instrument meaningful methods).
#define DESERT_PROFILE_FUNC()                                                                               \
    OPTICK_EVENT();                                                                                         \
    ::Common::Profiling::ScopedTimer DESERT_PROF_CONCAT( _desertProfFn_, __LINE__ )( __FUNCTION__ )

// Frame boundary: flip Optick's frame and publish the aggregator's last-frame snapshot. Call once/frame.
#define DESERT_PROFILE_FRAME( NAME )                                                                        \
    OPTICK_FRAME( NAME );                                                                                   \
    ::Common::Profiling::Profiler::Get().BeginFrame()
