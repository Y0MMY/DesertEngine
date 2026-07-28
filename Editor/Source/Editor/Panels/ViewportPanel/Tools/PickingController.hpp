#pragma once

#include <Engine/Desert.hpp>

#include <glm/glm.hpp>

namespace Desert::Editor::Tools
{
    // Object picking, extracted from ViewportPanel (god-object split). Casts the cursor ray against scene
    // meshes (engine-owned Scene::Raycast) and selects the hit entity (resolving to the prefab root) via
    // SelectionManager. No-op while the gizmo is hovered/dragged. The host gates mode / viewport-hover.
    class PickingController
    {
    public:
        // additive = Ctrl held: toggles the hit entity in the multi-selection instead of replacing it.
        // A miss with additive=false clears the selection (click empty space to deselect).
        void Pick( ::Desert::Core::Scene& scene, const glm::vec2& mouseViewport, const glm::vec2& viewportSize,
                   bool gizmoHovered, bool additive = false );
    };
} // namespace Desert::Editor::Tools
