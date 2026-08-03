#pragma once

#include <Engine/UI/UILayout.hpp>
#include <Engine/Graphic/Render2D/DrawList2D.hpp>

#include <entt/entt.hpp>

#include <string>

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
    };

    // Emit the scene's visible canvas into @p dl in pixel coordinates within @p viewportPx. When the canvas is
    // WorldSpace, @p worldViewProj (camera projection*view) billboards + distance-scales it to the screen;
    // pass nullptr for screen-space-only hosts. @p input drives button hover/press; a click on release writes
    // the button's encoded action to @p outClicked (see the runtime dispatcher). Returns false when there is
    // no visible canvas to draw.
    bool RenderCanvas2D( entt::registry& reg, Graphic::Render2D::DrawList2D& dl, const Rect& viewportPx,
                         const glm::mat4* worldViewProj = nullptr, const UIInput* input = nullptr,
                         std::string* outClicked = nullptr, entt::entity* focused = nullptr );
} // namespace Desert::UI
