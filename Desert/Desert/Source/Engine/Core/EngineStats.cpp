#include "EngineStats.hpp"

#include <GLFW/glfw3.h>

namespace Desert::Engine
{
    void EngineStats::Update()
    {
        float currentTime = static_cast<float>( glfwGetTime() );
        m_FrameTime       = Common::Timestep( currentTime - m_LastFrameTime );
        m_LastFrameTime   = Common::Timestep( currentTime );

        // FPS calculation logic remains the same
        m_FPSCounter++;
        m_FPSAccumulator += m_FrameTime.GetSeconds();
        if ( m_FPSAccumulator >= m_FPSUpdateInterval )
        {
            m_FPS            = static_cast<float>( m_FPSCounter ) / m_FPSAccumulator;
            m_FPSCounter     = 0;
            m_FPSAccumulator = 0.0f;
        }
    }

    std::string EngineStats::GetFormattedStats() const
    {
        return "FPS: " + std::to_string( static_cast<int>( m_FPS ) ) +
               " | Frame: " + std::to_string( GetFrameTimeMs() ) + "ms";
    }
} // namespace Desert::Engine