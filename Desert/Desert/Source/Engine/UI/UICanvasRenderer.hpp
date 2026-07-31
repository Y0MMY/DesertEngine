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
} // namespace Desert::UI
