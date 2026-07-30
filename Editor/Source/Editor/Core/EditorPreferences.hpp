#pragma once

#include <string>

#include <glm/glm.hpp>

namespace Desert::Editor
{
    // User-level editor settings, persisted to ~/.desertengine/editor.json (per-user, not per-project).
    // Loaded once at editor startup and applied to the live systems (GizmoState, editor camera); the
    // Preferences window (Edit -> Preferences...) edits + saves them.
    struct EditorPreferences
    {
        float CameraSpeed     = 1.0f;
        float TranslateSnap   = 0.5f;  // world units
        float RotateSnapDeg   = 15.0f; // degrees
        float ScaleSnap       = 0.1f;
        bool  PersistentSnap  = false;
        int   AutosaveMinutes = 5;     // 0 = autosave off
        bool  ShowPerfHud     = false; // in-viewport FPS / frame-graph / top-scopes overlay
        // Bumped when the default dock layout's window IDs change (e.g. panel-title icons add a ### suffix,
        // which changes every window's ImGui ID). A stored value below the current forces ONE automatic
        // "reset to default layout" so panels re-dock cleanly instead of scattering against a stale imgui.ini.
        int DockLayoutVersion = 0;
        // MSAA for the scene viewport (1 = off, 2/4/8). Applied at STARTUP (pipelines bake their
        // sample count): Load() pushes it into RenderConfig before the SceneRenderer initializes.
        int MSAASamples = 1;

        // Selection outline (Jump Flood) — an editor-only viewport visualization, not a scene property.
        // Pushed to the renderer each frame via SceneRenderer::SetOutlineSettings. Width/smoothness in px.
        glm::vec3 OutlineColor      = glm::vec3( 1.0f, 0.5f, 0.0f ); // orange
        float     OutlineWidth      = 4.0f;
        float     OutlineSmoothness = 2.0f;
        bool      EnableOutline     = true;

        // Photogrammetry (Model-from-Photos panel): TOOL-AGNOSTIC external commands. Reconstruct: {input} = the
        // photos folder, {output} = the produced mesh file, {outdir} = its directory (plug in Meshroom/COLMAP).
        // Capture: {photos} = the photos folder to fill with frames from the camera (plug in ffmpeg/your tool).
        std::string PhotogrammetryCommand = "meshroom_batch --input {input} --output {outdir}";
        std::string PhotogrammetryCaptureCommand =
             "ffmpeg -y -f avfoundation -framerate 2 -i 0 -t 20 -q:v 2 {photos}/frame_%03d.jpg";
        std::string PhotogrammetryPhotosDir  = "";
        std::string PhotogrammetryOutputMesh = "Cooked/Photogrammetry/model.obj";
        // 0 = Object (generic photogrammetry), 1 = Face (MetaHuman-style capture). Only swaps the tool
        // presets + on-screen guidance; the engine still delegates the heavy lifting to the external CLI.
        int PhotogrammetryMode = 0;

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
