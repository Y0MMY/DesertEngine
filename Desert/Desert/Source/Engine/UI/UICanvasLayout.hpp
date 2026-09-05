#pragma once

#include <Engine/UI/UILayout.hpp>

#include <entt/entt.hpp>

// Layout QUERIES over a canvas tree: where does an element end up on screen, and what is under the cursor.
//
// These used to live in UICanvasRenderer.{hpp,cpp} next to the ImGui draw path. That draw path is gone —
// the engine ships one canvas renderer (UICanvasRenderer2D, into a DrawList2D) — but the queries are not
// drawing and never were: the editor needs them to click-select a UI element in the viewport, to put a
// selection marquee around it and to convert an on-screen size back into design-space UILayout offsets.
// Splitting them out is also what takes ImGui out of Engine/ (a layering rule the old file broke).
//
// THE RELATION THAT MATTERS: these resolve a rect the same way RenderCanvas2D does — the canvas scale
// mode, the safe-area inset, the aspect fitter, the content-size fitter and auto-layout group placement.
// If the two ever drift, a click lands on nothing while the element is plainly on screen. Anything added
// to the renderer's rect resolution belongs here too.
namespace Desert::UI
{
    // In-scene UI editing (viewport WYSIWYG). Returns the topmost UI element whose resolved rect contains
    // `pointPx`, or entt::null. `viewportPx` must be the SAME rect the canvas was drawn into so hit-testing
    // matches what is on screen.
    entt::entity PickElement( entt::registry& reg, const glm::vec2& pointPx, const Rect& viewportPx );

    // Resolves the on-screen rect of a specific UI element (or the canvas itself) under the same layout the
    // renderer uses — for the selection marquee and the drag handles. false if not found.
    bool GetElementRect( entt::registry& reg, entt::entity target, const Rect& viewportPx, Rect& out );

    // The canvas's current uniform scale (design px -> screen px) for the given viewport, per its scale mode
    // (1 in Stretch). The editor divides on-screen sizes by this when writing UILayout offsets so a value it
    // computes from GetElementRect round-trips instead of being scaled twice. 1 if there is no canvas.
    float CanvasScale( entt::registry& reg, const Rect& viewportPx );
} // namespace Desert::UI
