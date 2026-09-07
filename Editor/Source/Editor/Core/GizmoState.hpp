#pragma once

namespace Desert::Editor::Core
{
    // Editor-global transform-gizmo mode, and the gizmo's view of the snap settings.
    //
    // Shared static (same pattern as SelectionManager / SkeletonEditMode) so BOTH the viewport (keyboard
    // W/E/R + the GizmoController that renders the gizmo) and the main toolbar buttons reach ONE place
    // without cross-panel plumbing.
    //
    // THIS CLASS STORES THE OPERATION AND NOTHING ELSE, and that is the point rather than an accident.
    // It used to hold the four snap values in private statics as well, which made them a SECOND copy of
    // four fields editor.json already owned — and the two copies had different writers. The toolbar's
    // magnet popup and the viewport's snap popup wrote HERE; the Preferences window wrote THERE;
    // EditorPreferences::Save() pushed THERE -> HERE as its first statement. Two consequences, and the
    // second is the expensive one:
    //
    //   * nothing that wrote this class ever reached the file, so a chosen step did not survive a restart;
    //   * any UNRELATED save — the Perf HUD toggle, an MSAA pick, a star in the Details panel — ran that
    //     push and silently reverted the step the user had just set, mid-session, with no message.
    //
    // So the snap values are read out of EditorPreferences below and stored only there. This class is a
    // seam, not a store: it exists so a gizmo can ask "what step am I on" without including the
    // preferences header, and so a discrete toolbar choice persists without every button remembering to.
    // A setter here is for a DISCRETE choice; a continuous control (a DragFloat) must edit the owning
    // field directly and save when the edit settles — see the note in GizmoState.cpp.
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

        // Deliberately NOT persisted: which handle you last dragged is where you are in a task, not a
        // preference, and every session starts in Select the way the editor's other modal state does.
        static Operation Get()
        {
            return s_Operation;
        }

        static void Set( Operation op )
        {
            s_Operation = op;
        }

        // Snap increments, owned by EditorPreferences (~/.desertengine/editor.json). Snapping is active
        // when the persistent toggle is ON, or while Ctrl is held — and Ctrl INVERTS the toggle (so with
        // snap-always on, Ctrl gives a temporary free drag).
        //
        // Each setter writes the owning field and persists it, but only when the value actually changes.
        static float TranslateSnap();
        static float RotateSnapDegrees();
        static float ScaleSnap();
        static void  SetTranslateSnap( float v );
        static void  SetRotateSnapDegrees( float v );
        static void  SetScaleSnap( float v );

        static bool PersistentSnap();
        static void SetPersistentSnap( bool on );

        // The effective "snap now?" answer given the current Ctrl state.
        static bool SnapActive( bool ctrlHeld )
        {
            return PersistentSnap() != ctrlHeld; // XOR: Ctrl temporarily inverts the toggle
        }

    private:
        inline static Operation s_Operation = Operation::None;
    };
} // namespace Desert::Editor::Core
