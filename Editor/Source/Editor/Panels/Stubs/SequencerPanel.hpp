#pragma once

#include "../IPanel.hpp"

#include <string>
#include <vector>

namespace Desert::Editor
{
    // VISUAL STUB (no real functionality yet): a cinematic sequencer / animation timeline — transport
    // row, time ruler, tracks with keyframe diamonds, a draggable playhead. The future cutscene/animation
    // tooling will grow out of this layout. Hidden by default; enable via View -> Sequencer.
    class SequencerPanel final : public IPanel
    {
    public:
        SequencerPanel();

        ImVec2 GetDefaultSize() const override
        {
            return ImVec2( 980.0f, 420.0f );
        }
        void OnUIRender() override;

    private:
        struct Track
        {
            std::string        Name;
            ImU32              Color;
            std::vector<float> Keys; // seconds
        };

        std::vector<Track> m_Tracks;
        float              m_Playhead   = 1.2f;  // seconds
        float              m_Duration   = 10.0f; // seconds
        float              m_PxPerSec   = 90.0f; // zoom
        bool               m_DraggingPlayhead = false;
    };
} // namespace Desert::Editor
