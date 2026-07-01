#pragma once

#include <Engine/Desert.hpp>

#include <glm/glm.hpp>

namespace Desert::Editor::Tools
{
    // ImGuizmo-based viewport gizmos, extracted from ViewportPanel (god-object split). Owns the current
    // operation + the "gizmo is being hovered/dragged" flag (so picking/painting can stand down). Context
    // (scene, selection, the scene-image rect) is passed in per frame; selection comes from SelectionManager.
    class GizmoController
    {
    public:
        // Values match ImGuizmo::OPERATION so the cast is free.
        enum class Operation
        {
            None      = -1,
            Translate = 7,
            Rotate    = 120,
            Scale     = 896,
        };

        void      SetOperation( Operation op ) { m_Op = op; }
        Operation GetOperation() const { return m_Op; }
        bool      IsActive() const { return m_Op != Operation::None; }
        bool      IsHovered() const { return m_Hovered; }
        void      ResetHovered() { m_Hovered = false; } // call once per frame before rendering the gizmo

        // Object transform gizmo on the current selection. viewportPos/Size = the rendered scene-image rect.
        void RenderObject( ::Desert::Core::Scene& scene, const glm::vec2& viewportPos,
                           const glm::vec2& viewportSize );
        // Bone gizmo (Skeleton Edit mode) — edits the selected bone's LocalBindTransform.
        void RenderBone( ::Desert::Core::Scene& scene, const glm::vec2& viewportPos,
                         const glm::vec2& viewportSize );

    private:
        Operation m_Op      = Operation::None;
        bool      m_Hovered = false;
    };
} // namespace Desert::Editor::Tools
