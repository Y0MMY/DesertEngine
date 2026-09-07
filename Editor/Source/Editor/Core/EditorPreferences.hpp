#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

// The viewport's debug/show state. An ENGINE type, because the engine's renderer is what consumes it —
// this header only says where the editor's persisted copy lives.
#include <Engine/Graphic/DebugViewState.hpp>

namespace Desert::Editor
{
    // User-level editor settings, persisted to ~/.desertengine/editor.json (per-user, not per-project).
    // Loaded once at editor startup and applied to the live systems (GizmoState, editor camera); the
    // Preferences window (Edit -> Preferences...) edits + saves them.
    //
    // WHAT BELONGS IN THIS FILE, in one sentence (К1): ONE PERSON'S COPY OF THE EDITOR — what a user's own
    // installation must remember between sessions and across every project, and whose value two people on
    // the same project may legitimately hold differently at the same moment. What does NOT belong: anything
    // a second person opening the project must see (that is the .deproj), anything that varies from level
    // to level (that is the .desce), and anything the SHIPPED RUNTIME needs — the packaged game never opens
    // this file, so a value put here is a value taken away from the player.
    //
    // The three-question procedure that decides where a NEW field goes, the argument for the order of the
    // questions, and the census that goes red when a field lands in the wrong file are all in
    // Desert/Tests/Engine/ConfigOwnership. That suite enumerates this struct through rfl::fields<> — the
    // same call Save() writes it with — so a field added here without a decision fails it immediately.
    //
    // AND THIS IS THE ONLY PER-USER SETTINGS STORE. `~/.desertengine` holds four neighbours and not one of
    // them is an alternative to this struct: `projects.json` and `engines.json` are cross-process
    // REGISTRIES shared with the launcher, `Layouts/*.ini` (and the working-directory `imgui.ini`) are
    // opaque ImGui dock state that ImGui itself writes and parses, and `asset_favorites.txt` is user state
    // that should have been fields here — it is filed as debt, not as precedent. A new per-user setting
    // goes in this struct; a new per-user FILE is a conversation with the owner.
    struct EditorPreferences
    {
        float CameraSpeed = 1.0f;
        // World units, and 1 world unit is 1 CENTIMETRE project-wide. This was 0.5f with the comment
        // "world units" from the metre era, and Load() pushes it straight into GizmoState — so the
        // preferences file has been overwriting GizmoState's own (correct) 50 cm default with half a
        // centimetre ever since the units migration, i.e. grid snap has effectively been off. Matches
        // GizmoState::s_TranslateSnap deliberately: two defaults for one value is what caused this.
        float TranslateSnap   = 50.0f; // cm — half a metre
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

        // Viewport Show flags + View Mode — grid, colliders, bounding boxes, wireframe, buffer views.
        // Edited by the viewport toolbar's "Show" popup and its View Mode dropdown, pushed to every scene's
        // renderer each frame via SceneRenderer::SetDebugView, and persisted here because it is the USER's
        // answer to "what am I looking at", not the level's. It used to live in the level file: 55 of 80
        // scenes shipped `ShowColliders: true` through git, and 72 of 77 overrode whatever grid setting the
        // person opening them had chosen. See Graphic/DebugViewState.hpp.
        //
        // EVERY FLAG DEFAULTS OFF, including the grid, and that is a deliberate departure from the old
        // SceneSettings default of `ShowGrid = true`. Two reasons: the repository's own scenes are 72:5
        // against the grid, so all-off is what "the editor looks the same after the migration" actually
        // means here; and it makes one rule — an overlay appears because YOU turned it on — instead of one
        // default per flag. Turning the grid on is one click in the Show popup and it then persists across
        // scenes and sessions, which is strictly more than the old behaviour offered.
        Graphic::DebugViewState DebugView;

        // Photogrammetry (Model-from-Photos panel): TOOL-AGNOSTIC external command. Reconstruct: {input} = the
        // photos folder, {output} = the produced mesh file, {outdir} = its directory (plug in Meshroom/COLMAP).
        //
        // TWO FIELDS THAT USED TO SIT HERE WERE DELETED BY К1, not moved: `PhotogrammetryCaptureCommand` and
        // `PhotogrammetryMode`. Both were serialized into every editor.json and neither was mentioned by a
        // single line of code outside this declaration — the `{photos}` capture substitution the first one
        // documented was never implemented (the panel writes frames itself), and the Object/Face preset
        // switch the second one documented does not exist. They were §1.3 dead settings, invisible because
        // this file had no readership census at all; Desert/Tests/Engine/ConfigOwnership is now that census.
        // Old preference files still carrying the two keys load unchanged — reflect-cpp ignores keys the
        // struct no longer has, so no migration is owed.
        std::string PhotogrammetryCommand    = "meshroom_batch --input {input} --output {outdir}";
        std::string PhotogrammetryPhotosDir  = "";
        std::string PhotogrammetryOutputMesh = "Cooked/Photogrammetry/model.obj";
        // Path to the dlib 68-point model (shape_predictor_68_face_landmarks.dat) for real face tracking on
        // the camera overlay. Empty / dlib-not-built => a placeholder overlay is drawn instead.
        std::string PhotogrammetryFaceModel = "";

        // --- Details panel ------------------------------------------------------------------------
        // Fields the user pinned to the top of Details, as "TypeName.FieldName" (e.g. "PointLightData.
        // Intensity"). Only reflected fields can be pinned — a hand-written component widget has no
        // field identity to key on.
        std::vector<std::string> FavouriteFields;
        // Component sections the user collapsed, by their registered name. Everything not listed is
        // expanded, so a fresh install behaves exactly like before this was persisted.
        std::vector<std::string> CollapsedComponents;

        static EditorPreferences& Get();

        // Membership helpers for the two lists above. Toggling SAVES immediately: these are single
        // clicks scattered through the panel, and losing them to a crash before the next explicit save
        // would be worse than the write.
        static bool IsFavouriteField( const std::string& key );
        static void ToggleFavouriteField( const std::string& key );
        static bool IsComponentCollapsed( const std::string& name );
        static void SetComponentCollapsed( const std::string& name, bool collapsed );

        // ~/.desertengine (created on demand); shared with the Project Hub's projects.json.
        static std::string ConfigDirectory();

        // Reads editor.json into Get() (keeps defaults when the file is missing/corrupt) and pushes the
        // values into GizmoState. The camera speed is applied by EditorLayer once a camera exists.
        static void Load();

        // Writes Get() to editor.json and pushes the snap values into GizmoState.
        // False when the preferences file could not be written (reason logged): the values are live in
        // this session but will not come back in the next one.
        static bool Save();
    };
} // namespace Desert::Editor
