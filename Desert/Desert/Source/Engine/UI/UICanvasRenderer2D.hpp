#pragma once

#include <Engine/UI/UICanvasContext.hpp>
#include <Engine/UI/UILayout.hpp>
#include <Engine/Graphic/Render2D/DrawList2D.hpp>

#include <entt/entt.hpp>

#include <string>
#include <vector>

// Render2D-backed canvas renderer — the replacement for the ImGui-based RenderCanvas. Walks the scene's
// UICanvas / UILayout tree and records geometry into a DrawList2D, which the Render2D GPU backend then
// batches into real draw calls. This slice draws flat-colour panels + button backgrounds (screen-space
// canvas); sprites / 9-slice / text / effects / clipping land in later slices until it reaches parity with
// the ImGui path, which is then removed.
namespace Desert::UI
{
    // Pointer/mouse state for button interaction, in the SAME pixel space as the canvas viewport (top-left
    // origin). The host (runtime input, editor viewport) supplies it; MouseReleased is the down->up edge so a
    // click fires on release over a button. Pass nullptr to RenderCanvas2D for a non-interactive draw (buttons
    // show their normal state) — e.g. the editor authoring view.
    struct UIInput
    {
        glm::vec2   MousePx       = { 0.0f, 0.0f };
        bool        MouseDown     = false;
        bool        MouseReleased = false;
        float       ScrollDelta   = 0.0f; // mouse-wheel notches this frame (+ = up); drives ScrollView
        std::string TypedText;            // UTF-8 chars typed this frame (drives the focused InputField)
        bool        Backspace = false;    // backspace pressed this frame
        bool        Tab       = false;    // Tab pressed: advance keyboard focus to the next focusable
        bool        Submit    = false;    // Enter pressed: activate the focused control (button/toggle/...)
    };

    // Emit the scene's visible canvas into @p dl in pixel coordinates within @p viewportPx. When the canvas is
    // WorldSpace, @p worldViewProj (camera projection*view) billboards + distance-scales it to the screen;
    // pass nullptr for screen-space-only hosts. @p input drives button hover/press; a click on release writes
    // the button's encoded action to @p outClicked (see the runtime dispatcher). Returns false when there is
    // no visible canvas to draw.
    // @p outMessages (optional) collects EVERY message the canvas fired this frame — pointer enter/exit,
    // press/release and drops — since more than one can happen in a single frame, unlike @p outClicked.
    // Without it those messages fall back to @p outClicked when it is still empty.
    //
    // @p ctx carries EVERYTHING this walk remembers between frames — hover and tween clocks, the elected hot
    // element, the drag, the screen stack. It belongs to the calling VIEW and must be the same object frame
    // after frame; see UICanvasContext.hpp for why one process-wide set of that state was a defect rather
    // than a simplification. Two views (two scene documents, or a viewport and the UI Editor preview) each
    // pass their own, and cannot then disturb each other.
    bool RenderCanvas2D( UICanvasContext& ctx, entt::registry& reg, Graphic::Render2D::DrawList2D& dl,
                         const Rect& viewportPx, const glm::mat4* worldViewProj = nullptr,
                         const UIInput* input = nullptr, std::string* outClicked = nullptr,
                         entt::entity* focused = nullptr, std::vector<std::string>* outMessages = nullptr );
} // namespace Desert::UI
