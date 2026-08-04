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

        // Snap increments. Snapping is active when the persistent toggle is ON, or while Ctrl is held —
        // and Ctrl INVERTS the toggle (so with snap-always on, Ctrl gives a temporary free drag).
        static float TranslateSnap()
        {
            return s_TranslateSnap;
        }
        static float RotateSnapDegrees()
        {
            return s_RotateSnapDeg;
        }
        static float ScaleSnap()
        {
            return s_ScaleSnap;
        }
        static void SetTranslateSnap( float v )
        {
            s_TranslateSnap = v;
        }
        static void SetRotateSnapDegrees( float v )
        {
            s_RotateSnapDeg = v;
        }
        static void SetScaleSnap( float v )
        {
            s_ScaleSnap = v;
        }

        static bool PersistentSnap()
        {
            return s_PersistentSnap;
        }
        static void SetPersistentSnap( bool on )
        {
            s_PersistentSnap = on;
        }

        // The effective "snap now?" answer given the current Ctrl state.
        static bool SnapActive( bool ctrlHeld )
        {
            return s_PersistentSnap != ctrlHeld; // XOR: Ctrl temporarily inverts the toggle
        }

    private:
        inline static Operation s_Operation = Operation::None;

        inline static float s_TranslateSnap  = 50.0f; // world units (cm) — half a metre
        inline static float s_RotateSnapDeg  = 15.0f; // degrees
        inline static float s_ScaleSnap      = 0.1f;
        inline static bool  s_PersistentSnap = false;
    };
} // namespace Desert::Editor::Core
