#pragma once

#include "../IPanel.hpp"

#include <memory>

namespace Desert::Core
{
    class Scene;
}
namespace Desert::Animation
{
    class AnimationLibrary;
    class Animator;
    struct AnimationClip;
}

namespace Desert::Editor
{
    // Animation sequencer / timeline for the SELECTED skinned-mesh entity: clip picker, transport
    // (play / pause / loop / speed), a scrubbable time ruler with the live playhead, animation-notify
    // markers, a keyframe TRACK EDITOR (per-bone position/rotation/scale lanes with draggable keys), and a
    // live layer-overlay preview. Drives the entity's AnimationComponent + Animator directly. Hidden by
    // default; enable via View -> Sequencer.
    class SequencerPanel final : public IPanel
    {
    public:
        SequencerPanel( std::shared_ptr<::Desert::Core::Scene> scene, Animation::AnimationLibrary* library );

        ImVec2 GetDefaultSize() const override
        {
            return ImVec2( 980.0f, 320.0f );
        }
        void OnUIRender() override;

    private:
        // Per-bone P/R/S keyframe lanes, aligned under the ruler (contentX0 = window content left, gutter =
        // label column, laneW = time area width). Handles select/drag/add/delete + a selected-key inspector.
        void DrawClipTracks( Animation::AnimationClip* clip, Animation::Animator* animator, float contentX0,
                             float gutter, float laneW, float duration );

        std::shared_ptr<::Desert::Core::Scene> m_Scene;
        Animation::AnimationLibrary*           m_Library = nullptr;

        float m_PxPerSec = 90.0f; // timeline zoom

        // Keyframe-editor selection (m_SelChannel: 0 = Position, 1 = Rotation, 2 = Scale).
        int   m_SelTrack   = -1;
        int   m_SelChannel = -1;
        int   m_SelKey     = -1;
        float m_DragTime   = 0.0f; // time being written while dragging a key (for re-selection after re-sort)

        // Layer-preview authoring state (transient — previews on the live Animator).
        int   m_LayerClip         = -1;
        float m_LayerWeight       = 1.0f;
        bool  m_LayerAdditive     = false;
        char  m_LayerMaskBone[64] = {};
    };
} // namespace Desert::Editor
