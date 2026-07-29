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
}

namespace Desert::Editor
{
    // Animation sequencer / timeline for the SELECTED skinned-mesh entity: clip picker, transport
    // (play / pause / loop / speed), a scrubbable time ruler with the live playhead, animation-notify
    // markers, and a live layer-overlay preview (override / additive with a bone mask). Drives the
    // entity's AnimationComponent + Animator directly. Hidden by default; enable via View -> Sequencer.
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
        std::shared_ptr<::Desert::Core::Scene> m_Scene;
        Animation::AnimationLibrary*           m_Library = nullptr;

        float m_PxPerSec = 90.0f; // timeline zoom

        // Layer-preview authoring state (transient — previews on the live Animator).
        int   m_LayerClip         = -1;
        float m_LayerWeight       = 1.0f;
        bool  m_LayerAdditive     = false;
        char  m_LayerMaskBone[64] = {};
    };
} // namespace Desert::Editor
