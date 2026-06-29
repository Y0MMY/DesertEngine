#include "Profiler.hpp"

#include <algorithm>

namespace Common::Profiling
{
    Profiler& Profiler::Get()
    {
        static Profiler s_Instance;
        return s_Instance;
    }

    void Profiler::BeginFrame()
    {
        std::lock_guard<std::mutex> lock( m_Mutex );

        const auto now = Clock::now();
        if ( !m_WindowStarted )
        {
            m_WindowStart   = now;
            m_WindowStarted = true;
        }
        ++m_WindowFrames;

        const double windowMs    = std::chrono::duration<double, std::milli>( now - m_WindowStart ).count();
        const double windowLimit = static_cast<double>( m_AvgWindowSec ) * 1000.0;
        if ( windowMs < windowLimit || m_WindowFrames == 0 )
            return; // still accumulating this window

        // Window elapsed: publish the AVERAGE per-frame cost of every scope over the window.
        const double inv = 1.0 / static_cast<double>( m_WindowFrames );
        m_Display.clear();
        m_Display.reserve( m_Accum.size() );
        for ( const auto& [name, result] : m_Accum )
        {
            ScopeResult avg;
            avg.Name    = name;
            avg.TotalMs = result.TotalMs * inv;                                  // avg ms / frame
            avg.Calls   = static_cast<uint32_t>( result.Calls * inv + 0.5 );     // avg calls / frame
            m_Display.push_back( avg );
        }
        if ( m_SortByTime )
            std::sort( m_Display.begin(), m_Display.end(),
                       []( const ScopeResult& a, const ScopeResult& b ) { return a.TotalMs > b.TotalMs; } );
        else
            std::sort( m_Display.begin(), m_Display.end(),
                       []( const ScopeResult& a, const ScopeResult& b ) { return a.Name < b.Name; } );

        m_DisplayFrameMs = windowMs * inv; // avg frame time over the window

        m_Accum.clear();
        m_WindowFrames = 0;
        m_WindowStart  = now;
    }

    void Profiler::AddSample( const char* name, double ms )
    {
        if ( !m_Enabled )
            return;

        std::lock_guard<std::mutex> lock( m_Mutex );
        auto&                       r = m_Accum[name]; // summed across the whole window, averaged on publish
        if ( r.Name.empty() )
            r.Name = name;
        r.TotalMs += ms;
        r.Calls += 1;
    }
} // namespace Common::Profiling
