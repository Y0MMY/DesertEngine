#pragma once

#include <string>

namespace Desert::Editor
{
    // User-level editor settings, persisted to ~/.desertengine/editor.json (per-user, not per-project).
    // Loaded once at editor startup and applied to the live systems (GizmoState, editor camera); the
    // Preferences window (Edit -> Preferences...) edits + saves them.
    struct EditorPreferences
    {
        float CameraSpeed      = 1.0f;
        float TranslateSnap    = 0.5f;  // world units
        float RotateSnapDeg    = 15.0f; // degrees
        float ScaleSnap        = 0.1f;
        bool  PersistentSnap   = false;
        int   AutosaveMinutes  = 5;     // 0 = autosave off
        bool  ShowPerfHud      = false; // in-viewport FPS / frame-graph / top-scopes overlay

        static EditorPreferences& Get();

        // ~/.desertengine (created on demand); shared with the Project Hub's projects.json.
        static std::string ConfigDirectory();

        // Reads editor.json into Get() (keeps defaults when the file is missing/corrupt) and pushes the
        // values into GizmoState. The camera speed is applied by EditorLayer once a camera exists.
        static void Load();

        // Writes Get() to editor.json and pushes the snap values into GizmoState.
        static void Save();
    };
} // namespace Desert::Editor
