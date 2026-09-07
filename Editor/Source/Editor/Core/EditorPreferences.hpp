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
    // Loaded once at editor startup. EVERY EDITOR OF THESE FIELDS PERSISTS ON THE SPOT — the Preferences
    // window commits each control when the user lets go of it, and the panels that own one field each
    // (the viewport's Show flags and snap steps, MSAA in Scene Settings, the stars in Details) commit on
    // the click. There is no "apply" step anywhere and there deliberately is not one; see Save().
    //
    // THIS STRUCT IS THE LIVE STATE, not a copy of it that something else has to be given. Everything
    // that consumes a preference reads it from here every time it needs it — the gizmo snap through
    // Core::GizmoState, the Details stars through the two helpers below, the Show flags straight off
    // DebugView. Exactly ONE value is pushed anywhere, RenderConfig::MSAASamples, because the layer that
    // reads it may not know the editor exists; see EditorPreferences.cpp for why that one is safe and
    // the four snap values were not.
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
        // THE GIZMO SNAP, AND THESE FOUR FIELDS ARE ITS ONLY STORAGE. Core::GizmoState reads and writes
        // them; it keeps no copy, and neither does anything else. It used to keep one, with these values
        // pushed into it by Save() — so an unrelated save reverted a step the user had just chosen, and
        // a step chosen from a toolbar never reached this file at all. К6 deleted the second copy rather
        // than adding a fourth Save() call; Desert/Tests/Editor/PreferenceOwnership holds the line.
        //
        // World units, and 1 world unit is 1 CENTIMETRE project-wide. TranslateSnap was 0.5f with the
        // comment "world units" from the metre era, which is how the shipped grid snap ended up at half
        // a centimetre; Load() migrates any stored value below 1 cm once.
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

        // --- Packaging (Build Settings) -------------------------------------------------------------
        // The three answers the Build Settings panel asks for. They are HERE and not in the panel, and
        // not in the .deproj, by the К1 procedure's first question: two people working on this project
        // at the same moment legitimately want different values for all three — an output path is a
        // place on one person's disk, which Runtime to bundle depends on whether that person is
        // debugging or cutting a playtest build, and a .app is one developer's convenience. None of
        // them is a fact about the product, and the shipped Runtime never reads them: they are consumed
        // by the editor BEFORE the game exists.
        //
        // The panel edits these in place and keeps no copy of its own. That is the rule this header
        // states above and the one К6 had to restore for the gizmo snap: a second copy means one of the
        // two stops being written, and which one is not visible from either side.
        //
        // The target PLATFORM is deliberately not among them. It is not a choice: this editor packages
        // for its own host and nothing else (Editor/Packaging/PackageTarget.hpp), so storing an answer
        // would be storing the only value it can have. П6 deleted the chooser that pretended otherwise.
        std::string PackageOutputDir = "Build/Output"; // relative to the editor cwd, or absolute
        std::string PackageConfig    = "Release";      // which Runtime binary to bundle: "Debug" | "Release"
        bool        PackageAppBundle = true;           // macOS: <Name>.app with MoltenVK inside

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

        // Reads editor.json into Get() (keeps defaults when the file is missing/corrupt). The camera
        // speed is applied by EditorLayer once a camera exists.
        //
        // IT WRITES THE FILE IN EXACTLY ONE CASE, and the case is named: when MigrateLoaded() below
        // raises a stored value, the new form is written back so the migration fires once instead of
        // every launch (contract §4.3). That write goes through SaveMigrated(), NOT through Save(), and
        // the difference is the point — a load that calls "save" is a reader that can write, and its log
        // line then claims a save the user never made. Nothing else in Load() touches the file.
        static void Load();

        // THE ONE MIGRATION THIS FILE CARRIES, AS A PURE FUNCTION. In: a preference set as it was read
        // from disk. Out: the same set in this build's form, plus one line per field it raised (empty =
        // there was nothing to do, which is what Load() tests before it writes anything). No file, no
        // globals, no logging — contract §4.4 asks a migration to be pure and tested, and
        // Desert/Tests/Editor/PreferenceOwnership calls this directly rather than through the file.
        static std::vector<std::string> MigrateLoaded( EditorPreferences& p );

        // THE USER JUST CHANGED SOMETHING. Called by every control that edits this struct, at the moment
        // the edit finishes (ImGui::IsItemDeactivatedAfterEdit for a slider or a drag, the click for a
        // checkbox or a menu item) — never on every frame of a drag, which would be sixty writes a second.
        //
        // It CHANGES NOTHING ELSE: the only thing it touches besides the file is RenderConfig::MSAASamples,
        // which is a one-way derived copy this file is the sole writer of. Anything else here would be a
        // save that edits state the user did not touch in the action that triggered it, which is what К6
        // removed.
        //
        // A SAVE THAT WOULD CHANGE NOTHING DOES NOT HAPPEN. The bytes are compared against what this
        // process believes is already on disk (and the file is confirmed still to be there, so the answer
        // can never be true of a file that is gone), and an identical write is skipped — no file write,
        // and no log line about one. That is what makes commit-on-edit affordable: letting go of a control you
        // only hovered, re-picking the MSAA level you are already on, or dragging a slider back to where
        // it started all cost nothing. True means "editor.json holds these values", which is as true of a
        // skipped write as of a performed one; false means it could not be written (reason logged) and the
        // values are live in this session only.
        static bool Save();

        // THE EDITOR, NOT THE USER, RAISED A STORED VALUE TO THIS BUILD'S FORM — a migration write-back,
        // and the only legitimate reason to write this file without a user action behind it. `what` is
        // the sentence that reaches the log in place of the changed-field list Save() derives, because a
        // migration is not a field the user moved and reporting it as one is how "[Prefs] Saved ..." came
        // to mean nothing. Same deduplication and same return meaning as Save().
        static bool SaveMigrated( const std::string& what );
    };
} // namespace Desert::Editor
