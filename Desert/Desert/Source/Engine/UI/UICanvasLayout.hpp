#pragma once

#include <Engine/UI/UILayout.hpp>

#include <Common/Core/Core.hpp>
#include <Common/Core/ResultStr.hpp>

#include <entt/entt.hpp>

#include <cstddef>

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
    // --- WHICH CANVAS. The question every one of these used to answer by itself, and always the same way --
    //
    // `*reg.view<UICanvasComponent>().begin()` — the first canvas entt happens to hand out — stood in three
    // places (the renderer's walk, the layout queries below, the editor's element factory) and it is not a
    // choice, it is a coincidence: entt's iteration order is a property of the pool, so "the first canvas"
    // is whichever one the scene file happened to create first. Everything downstream inherited it. A second
    // canvas was silently never drawn, never picked and never measured, which is why HUD and menu could not
    // be separated, why an overlay and a world-space canvas could not coexist, and why a prefab could not
    // carry its own canvas. The UI Editor could not preview the second canvas either and had to refuse by
    // name (U7-2 left that refusal in place, pointing here).
    //
    // So the canvas is now an ARGUMENT everywhere below and in RenderCanvas2D, and these three functions are
    // the only ways to obtain one. None of them can silently pick a winner:
    //
    //   CanvasOf     derives the answer from an element that already names it — its own canvas ancestor.
    //                This is what the editor uses, and it is exact rather than lucky.
    //   CanvasCount  counts. Counting is not electing, and it is what lets a host say "no canvas yet, offer
    //                to create one" and "more than one, ask which" as two different sentences.
    //   SoleCanvas   the answer for a host that has no other way to name one (the game, the viewport pass).
    //                It REFUSES when there is none and when there is more than one, with the count in the
    //                message — because "there are two and I drew one of them" is exactly the silent wrong
    //                answer the contract forbids, and it is what this code did for its whole life.

    // The canvas @p e belongs to: @p e itself when it carries a UICanvasComponent, otherwise the nearest
    // ancestor that does. entt::null when @p e is not under a canvas at all (a plain 3D entity, or a UI
    // element that has not been parented yet). Cycle-safe: the walk is bounded by the entity count.
    [[nodiscard]] entt::entity CanvasOf( entt::registry& reg, entt::entity e );

    // How many UICanvasComponents this scene holds.
    [[nodiscard]] std::size_t CanvasCount( entt::registry& reg );

    // The scene's ONE canvas. Refuses, by name and with the count, when there is not exactly one.
    [[nodiscard]] Common::ResultStr<entt::entity> SoleCanvas( entt::registry& reg );

    // --- The visibility axis, asked once ------------------------------------------------------------
    // Three places resolve an element's rect — the renderer's walk, the editor's pick, the editor's
    // marquee — and a fourth measures a container's content for the size fitter. All four have to agree
    // on WHICH CHILDREN EXIST, because a child counted by the layout and not by the pick is an element
    // drawn where nothing can click it, which is this project's recurring defect shape. So the two
    // questions are asked through these two functions and nowhere else.

    // Does @p e occupy a slot in its parent's auto-layout group? Only ECS::UIVisibility::Collapsed drops
    // out; Hidden keeps its slot, and that difference IS the layout axis. An element with no UILayout has
    // nothing to say and takes its slot.
    [[nodiscard]] bool TakesLayoutSpace( entt::registry& reg, entt::entity e );

    // Is @p e drawn at all — and therefore hit-testable at all? False for Hidden and Collapsed, both of
    // which take their whole sub-tree with them.
    [[nodiscard]] bool IsElementVisible( entt::registry& reg, entt::entity e );

    // In-scene UI editing (viewport WYSIWYG). Returns the topmost element of @p canvas whose resolved rect
    // contains `pointPx`, or entt::null. `viewportPx` must be the SAME rect the canvas was drawn into so
    // hit-testing matches what is on screen. A host with several canvases asks each one and keeps the last
    // hit — which is what makes an overlay in front of a HUD pickable at all.
    [[nodiscard]] entt::entity PickElement( entt::registry& reg, entt::entity canvas, const glm::vec2& pointPx,
                                            const Rect& viewportPx );

    // Resolves the on-screen rect of @p target (an element of @p canvas, or @p canvas itself) under the same
    // layout the renderer uses — for the selection marquee and the drag handles. false if @p target is not
    // in that canvas's tree, which is now a MEANINGFUL false: asking the wrong canvas is a caller error and
    // no longer silently answers about somebody else's element.
    [[nodiscard]] bool GetElementRect( entt::registry& reg, entt::entity canvas, entt::entity target,
                                       const Rect& viewportPx, Rect& out );

    // @p canvas's current uniform scale (design px -> screen px) for the given viewport, per its scale mode
    // (1 in Stretch). The editor divides on-screen sizes by this when writing UILayout offsets so a value it
    // computes from GetElementRect round-trips instead of being scaled twice.
    //
    // IT REFUSES RATHER THAN ANSWERING 1. This used to elect a canvas itself and return 1 when it found none
    // — and 1 is a perfectly plausible scale (it is what Stretch gives), so a caller could not tell a real
    // answer from "there was nothing to measure" and would write offsets scaled by the wrong factor.
    [[nodiscard]] Common::ResultStr<float> CanvasScale( entt::registry& reg, entt::entity canvas,
                                                        const Rect& viewportPx );
} // namespace Desert::UI
