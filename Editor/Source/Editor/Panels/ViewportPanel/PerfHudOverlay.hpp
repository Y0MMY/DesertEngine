#pragma once

#include <imgui.h>

namespace Desert::Editor
{
    // Compact in-viewport performance overlay (top-right): FPS + frame time, a scrolling frame-time
    // graph, and the top CPU scopes from the engine's Profiler aggregator. Toggled via View -> Perf HUD
    // (persisted in EditorPreferences). Pure ImGui draw-list overlay — no engine-side cost when hidden.
    class PerfHudOverlay
    {
    public:
        // Call inside the viewport window; viewportMin/Max are the content-region screen bounds.
        void Draw( const ImVec2& viewportMin, const ImVec2& viewportMax );

    private:
        static constexpr int kHistory = 180; // ~3 seconds at 60 FPS

        float m_FrameMs[kHistory] = {};
        int   m_Head              = 0;
        bool  m_Filled            = false;
    };
} // namespace Desert::Editor
