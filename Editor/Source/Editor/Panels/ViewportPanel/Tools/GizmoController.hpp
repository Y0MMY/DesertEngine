#pragma once

#include <Engine/Desert.hpp>
#include <Editor/Core/GizmoState.hpp>
#include <Editor/Core/Commands/SceneCommands.hpp>

#include <Common/Core/UUID.hpp>

#include <glm/glm.hpp>

#include <vector>

namespace Desert::Editor::Tools
{
    // ImGuizmo-based viewport gizmos, extracted from ViewportPanel (god-object split). The current operation
    // lives in the editor-global Core::GizmoState (shared with the main toolbar); this owns only the
    // "gizmo is being hovered/dragged" flag (so picking/painting can stand down). Context (scene, selection,
    // the scene-image rect) is passed in per frame; selection comes from SelectionManager.
    class GizmoController
    {
    public:
        // Alias the shared state's enum so existing GizmoController::Operation::X call sites keep compiling.
        using Operation = Core::GizmoState::Operation;

        void      SetOperation( Operation op ) { Core::GizmoState::Set( op ); }
        Operation GetOperation() const { return Core::GizmoState::Get(); }
        bool      IsActive() const { return Core::GizmoState::Get() != Operation::None; }
        bool      IsHovered() const { return m_Hovered; }
        void      ResetHovered() { m_Hovered = false; } // call once per frame before rendering the gizmo

        // Object transform gizmo on the current selection. viewportPos/Size = the rendered scene-image rect.
        void RenderObject( ::Desert::Core::Scene& scene, const glm::vec2& viewportPos,
                           const glm::vec2& viewportSize );
        // Bone gizmo (Skeleton Edit mode) — edits the selected bone's LocalBindTransform.
        void RenderBone( ::Desert::Core::Scene& scene, const glm::vec2& viewportPos,
                         const glm::vec2& viewportSize );

    private:
        bool m_Hovered = false;

        // One undo entry per gizmo drag: pre-drag TRS of every selected top-level root captured when the
        // drag starts, committed as one (possibly composite) command on release.
        bool                                     m_DragActive = false;
        Common::UUID                             m_DragEntity = Common::UUID::Null(); // primary at drag start
        std::vector<Commands::TransformSnapshot> m_DragSnapshots;
    };
} // namespace Desert::Editor::Tools
