#pragma once

#include <Engine/UI/UILayout.hpp>
#include <Engine/Graphic/Render2D/DrawList2D.hpp>

#include <entt/entt.hpp>

// Render2D-backed canvas renderer — the replacement for the ImGui-based RenderCanvas. Walks the scene's
// UICanvas / UILayout tree and records geometry into a DrawList2D, which the Render2D GPU backend then
// batches into real draw calls. This slice draws flat-colour panels + button backgrounds (screen-space
// canvas); sprites / 9-slice / text / effects / clipping land in later slices until it reaches parity with
// the ImGui path, which is then removed.
namespace Desert::UI
{
    // Emit the scene's visible canvas into @p dl in pixel coordinates within @p viewportPx. When the canvas is
    // WorldSpace, @p worldViewProj (camera projection*view) billboards + distance-scales it to the screen;
    // pass nullptr for screen-space-only hosts. Returns false when there is no visible canvas to draw.
    bool RenderCanvas2D( entt::registry& reg, Graphic::Render2D::DrawList2D& dl, const Rect& viewportPx,
                         const glm::mat4* worldViewProj = nullptr );
} // namespace Desert::UI
