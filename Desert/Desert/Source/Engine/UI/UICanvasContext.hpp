#pragma once

#include <Engine/ECS/Components.hpp>
#include <Engine/UI/UILayout.hpp>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// The runtime state of ONE canvas view.
//
// WHY THIS TYPE EXISTS. All of it used to be file-scope variables inside UICanvasRenderer2D.cpp, i.e. one
// set per process, and the engine draws more than one canvas per frame:
//
//   * the editor builds a Render::RenderRegistry — and with it an EditorUIPass — per open scene document,
//     so two viewports walk two scenes into the same variables in one frame;
//   * the UI Editor panel walks the SAME scene a second time into its own offscreen target. That walk is
//     deliberately non-interactive (input = nullptr), but it still ran the hand-over at the end of the walk
//     and so cleared the viewport's elected hot element every frame the panel was open. That one needs no
//     second scene to reproduce;
//   * entt::entity is unique only INSIDE its registry, so the per-entity clocks below (hover, tween) had a
//     key that entity 7 of scene A and entity 7 of scene B both answered to. Same shape as the pipeline
//     cache key that dropped five fields.
//
// The fix is state per (view x frame) exactly as Docs/RENDERER_FRAME_STATE.md requires of the renderer: a
// host owns one context for as long as it owns its view, hands it to every RenderCanvas2D call it makes,
// and two views cannot reach each other's state because they never share the object. The per-entity maps
// live in here, so the key is (context, entity) and the registry question does not arise; the context
// additionally drops them when it is pointed at a different registry, which is the one way a single view
// could still mix two scenes.
//
// WHAT DELIBERATELY DID NOT MOVE HERE. Runtime UI state stays OUT of the ECS components (UI_ROADMAP.md
// section F): navigating screens or easing a hover in the editor must not rewrite the authored scene. This
// type is that decision made explicit, not a reversal of it.
namespace Desert::UI
{
    // An in-flight drag. Cross-frame: a drag survives from the press that starts it to the release that
    // drops it, so it belongs to the view rather than to the walk.
    struct UIDragState
    {
        bool         Active  = false; // past the threshold: the ghost is up and a drop can land
        bool         Pending = false; // pressed on a draggable, still deciding drag vs click
        entt::entity Source  = entt::null;
        std::string  Payload;
        glm::vec2    Size{ 0.0f };
        glm::vec2    PressPos{ 0.0f };
        float        Ghost = 0.55f;
    };

    struct UICanvasContext
    {
        // --- Identity ---------------------------------------------------------------------------------
        // The registry this state describes, remembered so a view that is pointed at another scene starts
        // clean instead of reading its predecessor's entity ids. Compared by address and never dereferenced.
        // A host that swaps scenes should also call Reset(): a freed registry's address can be reused by the
        // next one, which would make this comparison a false negative.
        const entt::registry* Registry = nullptr;

        // --- This view's clock ------------------------------------------------------------------------
        // Wall-clock seconds at this view's previous frame, and the delta from it. Per view because two
        // views draw in the same frame: with one shared clock the second walk of every frame measured a
        // delta of ~0 and its hover eases, tweens and screen transitions stood still.
        // HasDrawn is a flag rather than a sentinel value in LastFrameTime, because the clock's epoch is the
        // first UI frame of the process: a "not yet" spelled as a negative time is indistinguishable from a
        // real reading taken in the first fraction of a second.
        bool     HasDrawn      = false;
        float    LastFrameTime = 0.0f;
        float    FrameDt       = 0.0f;
        uint64_t FrameIndex    = 0; // drives the tween's rewind-on-hide check

        // Does this view drive the scene's SHARED animation playheads (UIAnimComponent::Data::Time)? That
        // one clock lives in the component on purpose — the Sequencer scrubs it — so it is scene state, not
        // view state, and exactly one view may advance it. A second view advancing it too runs every UIAnim
        // clip at double speed. The authoring preview sets this false; the viewport / game keeps it true.
        bool DrivesSceneAnimation = true;

        // --- Per-entity clocks (transient, never serialized) -------------------------------------------
        std::unordered_map<entt::entity, float>    HoverT;    // 0 = rest, 1 = hovered; eased each frame
        std::unordered_map<entt::entity, float>    TweenT;    // per-element tween playhead
        std::unordered_map<entt::entity, uint64_t> TweenSeen; // FrameIndex the tween was last evaluated on

        // --- Hit testing ------------------------------------------------------------------------------
        // The walk elects a single HOT element (last writer in draw order = topmost) and controls compare
        // against the PREVIOUS frame's winner, the same one-frame deferral ImGui uses.
        entt::entity Hot     = entt::null; // resolved last frame: what the controls react to now
        entt::entity HotNext = entt::null; // being elected during this frame's walk
        Rect         HotNextRect{};
        bool         PrevDown = false; // for the press edge (UIInput only carries held + release)
        UIDragState  Drag;

        // --- Screens ----------------------------------------------------------------------------------
        // Which of the canvas's UIScreen sub-trees is current, and the hand-over running between two of
        // them. Per view: one viewport navigating to Settings must not move another view of the same scene.
        std::string              Screen;                // current screen name
        std::string              ScreenFrom;            // the one handing over, while a transition runs
        std::vector<std::string> ScreenStack;           // history for BackScreen
        float                    ScreenT       = 1.0f;  // 0..1 progress of the transition (1 = idle)
        float                    ScreenSlidePx = 60.0f; // mirrored from the canvas's UIScreenStack
        float                    ScreenTime    = 0.25f;
        ECS::UIEasing            ScreenEasing  = ECS::UIEasing::CubicOut;
        bool                     ScreenBack    = false; // a Back transition slides the other way
        std::string              ScreenReq;             // requested by a button, applied at the walk's end
        bool                     ScreenReqBack = false;

        // --- Walk-local -------------------------------------------------------------------------------
        // The tint of the element being drawn, multiplied into its colours so Opacity/Color tweens reach
        // every control. Saved and restored by the recursion, so it enters and leaves each walk at 1.
        glm::vec4 Tint{ 1.0f };

        // The background sprite this view last refused to draw, so an unresolvable handle is reported once
        // instead of once per frame. Cleared when the handle changes or resolves.
        Assets::AssetHandle WarnedBackground;

        // Forget everything about the scene drawn so far. Hosts call this when they point the view at a
        // different scene; RenderCanvas2D calls it itself when it notices the registry changed.
        void Reset()
        {
            Registry = nullptr;
            HoverT.clear();
            TweenT.clear();
            TweenSeen.clear();
            Hot         = entt::null;
            HotNext     = entt::null;
            HotNextRect = Rect{};
            PrevDown    = false;
            Drag        = UIDragState{};
            Screen.clear();
            ScreenFrom.clear();
            ScreenStack.clear();
            ScreenT    = 1.0f;
            ScreenBack = false;
            ScreenReq.clear();
            ScreenReqBack    = false;
            Tint             = glm::vec4( 1.0f );
            WarnedBackground = Assets::AssetHandle{};
        }
    };
} // namespace Desert::UI
