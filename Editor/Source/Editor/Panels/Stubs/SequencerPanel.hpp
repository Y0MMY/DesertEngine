#pragma once

#include "../IPanel.hpp"

#include <Engine/ECS/Entity.hpp>

#include <glm/glm.hpp>

#include <memory>

namespace Desert::Core
{
    class Scene;
}
namespace Desert::Assets
{
    class AssetManager;
}
namespace Desert::Animation
{
    class AnimationLibrary;
    class Animator;
    class Skeleton;
    struct AnimationClip;
} // namespace Desert::Animation

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
        SequencerPanel( std::shared_ptr<::Desert::Core::Scene> scene, Animation::AnimationLibrary* library,
                        Assets::AssetManager* assetManager );

        ImVec2 GetDefaultSize() const override
        {
            return ImVec2( 980.0f, 320.0f );
        }
        void OnUIRender() override;

        // NOT contextual, deliberately. A timeline is a place you GO TO author a clip, not a thing that
        // should appear because you clicked a character in the viewport: selecting an animated actor to
        // move it would throw a full-width editor over the screen every time. It opens on request only —
        // Details ▸ Animation ▸ "Open in Sequencer", the View menu, or the command palette.
        bool IsContextual() const override
        {
            return false;
        }
        bool IsRelevant() const override;
        void OnPreUpdate() override;

        // Cross-panel: a Details "Open in Sequencer" button asks the panel to reveal itself next frame.
        static void RequestOpen();

    private:
        // Per-bone P/R/S keyframe lanes, aligned under the ruler (contentX0 = window content left, gutter =
        // label column, laneW = time area width). Handles select/drag/add/delete + a selected-key inspector.
        void DrawClipTracks( Animation::AnimationClip* clip, Animation::Animator* animator, float contentX0,
                             float gutter, float laneW, float duration );

        // Creates a NEW empty clip for the given skeleton (a track per bone, no keys yet), registers it as an
        // in-memory AnimationAsset so it shows in the picker, and returns its name (empty on failure).
        std::string CreateEmptyClip( const Animation::Skeleton& skeleton );

        // Writes the clip to Cooked/Meshes/_<name>.anim (rfl::json, same format the importer cooks) so an
        // in-editor-authored clip PERSISTS and is rediscovered by the AssetPreloader next session. Returns the
        // written path (empty on failure).
        std::string SaveClipToDisk( const Animation::AnimationClip& clip );

        // Records the bone's CURRENT local transform (posed in the viewport via Skeleton Edit) as position +
        // rotation + scale keyframes at `time` in `clip` (upserting any key already at that time). This is the
        // "keyframe by manipulation" path: pose with the gizmo, then key. boneIndex is a Skeleton bone index.
        void KeyBonePose( Animation::AnimationClip* clip, const Animation::Animator& animator, int boneIndex,
                          float time );

        // UI mode: the selected entity is a UI element rather than a skinned mesh. Draws the clip's
        // property lanes (Offset / Size / Opacity / Color) with draggable keys and a scrubbable playhead.
        void DrawUITracks( ECS::Entity& entity );

        std::shared_ptr<::Desert::Core::Scene> m_Scene;
        Animation::AnimationLibrary*           m_Library      = nullptr;
        Assets::AssetManager*                  m_AssetManager = nullptr;

        float m_PxPerSec = 90.0f; // timeline zoom

        // Record mode: while ON (and in Skeleton Edit), moving the selected bone with the gizmo AUTO-keys it at
        // the playhead. m_RecordBone/m_RecordLast track the last-seen transform to detect a change.
        bool      m_Record     = false;
        int       m_RecordBone = -1;
        glm::mat4 m_RecordLast = glm::mat4( 1.0f );

        // Keyframe-editor selection (m_SelChannel: 0 = Position, 1 = Rotation, 2 = Scale).
        int   m_SelTrack   = -1;
        int   m_SelChannel = -1;
        int   m_SelKey     = -1;
        float m_DragTime   = 0.0f; // time being written while dragging a key (for re-selection after re-sort)

        // UI-clip editing state (which lane/key is selected in UI mode).
        int m_UITrack = -1;
        int m_UIKey   = -1;

        // Layer-preview authoring state (transient — previews on the live Animator).
        int   m_LayerClip         = -1;
        float m_LayerWeight       = 1.0f;
        bool  m_LayerAdditive     = false;
        char  m_LayerMaskBone[64] = {};
    };
} // namespace Desert::Editor
