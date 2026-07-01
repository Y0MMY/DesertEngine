#pragma once

namespace Desert::Editor::Core
{
    // Editor-global current transform-gizmo mode. Shared static (same pattern as SelectionManager /
    // SkeletonEditMode) so BOTH the viewport (keyboard W/E/R + the GizmoController that renders the gizmo)
    // and the main toolbar buttons read/write ONE source of truth without cross-panel plumbing.
    class GizmoState
    {
    public:
        // Values match ImGuizmo::OPERATION so the GizmoController cast stays free.
        enum class Operation
        {
            None      = -1,
            Translate = 7,
            Rotate    = 120,
            Scale     = 896,
        };

        static Operation Get()
        {
            return s_Operation;
        }

        static void Set( Operation op )
        {
            s_Operation = op;
        }

    private:
        inline static Operation s_Operation = Operation::None;
    };
} // namespace Desert::Editor::Core
