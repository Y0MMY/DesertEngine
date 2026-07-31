#pragma once

#include <Engine/UI/UILayout.hpp>

#include <entt/entt.hpp>

#include <string>

struct ImDrawList;

namespace Desert::UI
{
    // Draws the scene's FIRST UICanvas + its UILayout/UIPanel/UIText/UIButton child tree into `dl`, letterboxed
    // to the canvas design resolution inside `viewportPx` (screen pixels). SHARED by the editor's UI Editor
    // panel (authoring/preview) and the game Runtime (in-game menu) so the exact same layout renders in both.
    //
    // interactive: buttons tint on hover / press. outClicked (optional): a button clicked THIS frame writes its
    // OnClickMessage here (empty otherwise) — the caller acts on it (e.g. "scene:MainLevel" -> load a scene).
    // Returns true if a canvas was found + drawn.
    bool RenderCanvas( entt::registry& reg, ImDrawList* dl, const Rect& viewportPx, bool interactive,
                       std::string* outClicked = nullptr );

    // In-scene UI editing (viewport WYSIWYG). Returns the topmost UI element (Panel/Text/Button) whose
    // resolved rect contains `pointPx`, or entt::null. `viewportPx` must be the SAME rect passed to
    // RenderCanvas so hit-testing matches what is drawn. Used to click-select UI in the 3D viewport.
    entt::entity PickElement( entt::registry& reg, const glm::vec2& pointPx, const Rect& viewportPx );

    // Resolves the on-screen rect of a specific UI element (or the canvas itself) under the same layout as
    // RenderCanvas — used to draw a selection marquee around the selected element. false if not found.
    bool GetElementRect( entt::registry& reg, entt::entity target, const Rect& viewportPx, Rect& out );
} // namespace Desert::UI
