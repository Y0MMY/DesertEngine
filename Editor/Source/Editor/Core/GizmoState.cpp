#include "GizmoState.hpp"

#include <Editor/Core/EditorPreferences.hpp>

namespace Desert::Editor::Core
{
    namespace
    {
        // WHY THE SETTERS PERSIST AND THE GETTERS DO NOT COPY.
        //
        // The four snap values have one storage — the EditorPreferences instance, which is the in-memory
        // image of ~/.desertengine/editor.json. Reading them here is a read of that image; writing them
        // here is a write to it, and a write nobody persists is the defect this seam was built to close
        // (a step chosen from the toolbar was live for the session and gone on the next launch).
        //
        // Saving on the spot is the same judgement EditorPreferences::ToggleFavouriteField already
        // records for the Details stars: these are single clicks scattered across two toolbars, and
        // losing one to a crash before some later explicit save would be worse than the write.
        //
        // The equality guard is not an optimisation — it is what keeps that true. Re-picking the step
        // that is already selected must not rewrite the file, and must not log a save that happened for
        // nothing; ImGui::Selectable reports a click whether or not it changed anything.
        //
        // AND THIS PATH IS FOR DISCRETE CHOICES ONLY. A DragFloat reports a new value on every frame it
        // moves, so routing one through here would write editor.json (and log a line) sixty times a
        // second. A continuous control must bind to the owning field directly and call
        // EditorPreferences::Save() from ImGui::IsItemDeactivatedAfterEdit(); the viewport's snap popup
        // is the worked example. Binding to a local and writing back through a setter does not work for
        // a different reason: DragFloat accumulates the drag IN the value it is given, so a local
        // re-seeded from the owner every frame never moves.
        template <class T>
        void SetAndPersist( T& field, T value )
        {
            if ( field == value )
                return;
            field = value;
            EditorPreferences::Save();
        }
    } // namespace

    float GizmoState::TranslateSnap()
    {
        return EditorPreferences::Get().TranslateSnap;
    }

    float GizmoState::RotateSnapDegrees()
    {
        return EditorPreferences::Get().RotateSnapDeg;
    }

    float GizmoState::ScaleSnap()
    {
        return EditorPreferences::Get().ScaleSnap;
    }

    bool GizmoState::PersistentSnap()
    {
        return EditorPreferences::Get().PersistentSnap;
    }

    void GizmoState::SetTranslateSnap( float v )
    {
        SetAndPersist( EditorPreferences::Get().TranslateSnap, v );
    }

    void GizmoState::SetRotateSnapDegrees( float v )
    {
        SetAndPersist( EditorPreferences::Get().RotateSnapDeg, v );
    }

    void GizmoState::SetScaleSnap( float v )
    {
        SetAndPersist( EditorPreferences::Get().ScaleSnap, v );
    }

    void GizmoState::SetPersistentSnap( bool on )
    {
        SetAndPersist( EditorPreferences::Get().PersistentSnap, on );
    }
} // namespace Desert::Editor::Core
