#pragma once

#include <Engine/Desert.hpp>
#include <Engine/Runtime/AssetHotReload.hpp>
#include <ImGui/imgui.h>
#include "Editor/Widgets/UIHelper/ImGuiUI.hpp"
#include "Editor/Panels/IPanel.hpp"
#include "Editor/Core/CommandPalette.hpp"
#include "Editor/Core/SceneViewIdentity.hpp"
#include "Editor/Core/AssetEditorRegistry.hpp"
#include "Editor/Core/DocumentWell.hpp"
#include "Editor/Core/PanelRegistry.hpp"
#include "Editor/RenderSystems/RenderRigistry.hpp"

#include <filesystem>

namespace Desert::Editor
{
    class ImportManager;
    class FileExplorerPanel;
    class ViewportPanel;

    class EditorLayer : public Common::Layer
    {
    public:
        explicit EditorLayer( const Engine::Application* window, const std::string& layerName );
        ~EditorLayer();

        [[nodiscard]] virtual Common::BoolResultStr OnAttach() override;
        [[nodiscard]] virtual Common::BoolResultStr OnDetach() override;
        [[nodiscard]] virtual Common::BoolResultStr OnUpdate( const Common::Timestep& ts ) override;
        [[nodiscard]] virtual Common::BoolResultStr OnImGuiRender() override;
        virtual void                                OnEvent( Common::Event& event ) override;

    private:
        void DrawMenuBar();

        // ===== Menus =====
        void DrawFileMenu();
        void DrawEditMenu();
        void DrawViewMenu();
        // Window ▸ Documents: the open documents, focused with a RADIO and closed with an x. A radio and
        // not a checkbox on purpose — a tick reads as "shown / hidden", which is the very thing a document
        // cannot be. See DocumentWell.
        void DrawWindowMenu();
        void DrawScenesMenu();
        void DrawGraphicsMenu();
        void DrawAboutMenu();

        // ===== Menu sections =====
        void DrawStyleSubmenu();
        void DrawOpenSceneMenuItem();
        void DrawPreferencesWindow(); // Edit -> Preferences... (persisted to ~/.desertengine/editor.json)

        // ===== Top Bar Sections =====
        void DrawProjectSection();
        void DrawSceneRenameSection();
        void DrawPlayButton( const ImVec2& size = ImVec2( 0.0f, 0.0f ) );
        void DrawPauseButton( const ImVec2& size = ImVec2( 0.0f, 0.0f ) );

        // UE5-style toolbar strip below the menu bar. Left: save + undo/redo, editor modes, transform
        // tools, the two snap steps. Centre: playback. Right: package, profiler, preferences. Drawn inside
        // the dockspace host window so it takes a fixed height above the docked panels.
        //
        // Every control here writes to state that already has one owner elsewhere (CommandHistory,
        // ViewportMode, GizmoState, Scene::GetState) — the bar reports and commands, it never stores.
        void DrawToolbar();
        // One toolbar button. `active` is the armed/on state: tinted fill plus a 2px underline.
        bool ToolbarButton( const char* icon, const char* label, bool active = false,
                            const char* tooltip = nullptr, bool enabled = true );
        void ToolbarSeparator();
        // A snap step: the button reports the current step and opens the list that changes it, with the
        // shared snapping toggle at the top. `rotation` picks the angle step over the grid step.
        void DrawSnapControl( bool rotation );

        // Bottom status bar: scene state (Edit/Play), scene name, current selection, and FPS/frame time.
        void DrawStatusBar();
        // Triangles drawn by the scene's meshes, summed over entities. Cached — see m_TriangleCache.
        uint64_t SceneTriangleCount();
        // The status bar's console line (UE's "Enter Console Command"); handed to the Lua console to run.
        char m_StatusCmd[256] = {};

        // Triangle census for the status bar. Walking every entity's submeshes each frame is cheap on a
        // 24-entity scene and is not on a large one, so the answer is cached and recomputed on the two
        // things that can change it: an edit (the revision moves) and an entity appearing or vanishing.
        // A mesh finishing an ASYNC load bumps neither, so the cache also has a frame budget — a count
        // that is three seconds stale is a status bar; a count that is permanently wrong is a lie.
        uint64_t m_TriangleCache      = 0;
        uint64_t m_TriangleCacheRev   = static_cast<uint64_t>( -1 );
        size_t   m_TriangleCacheCount = static_cast<size_t>( -1 );
        int      m_TriangleCacheAge   = 0;
        // Opens/closes panels whose context appeared or vanished (see IPanel::IsContextual).
        void UpdateContextualPanels();

        // Ctrl+P "go to anything": builds the frame's commands (panels, entities, actions) and draws
        // the overlay. No-op unless the palette is open.
        void DrawCommandPalette();

        // After an unclean exit, offers to reopen the newest autosave. No-op unless one was found.
        void DrawRecoveryPopup();

        // Modal for naming + saving the current docking layout (opened from View -> Layouts).
        void DrawLayoutSavePopup();

        // Play mode: snapshot the scene on Play, restore it on Stop (so play-time changes don't persist).
        void OnScenePlay();
        void OnSceneStop();
        void OnScenePauseToggle();

        // Builds a ready-to-Play demo: a WASD character (Jolt CharacterVirtual) with a 3rd-person child
        // camera, a ground floor, a sun light, and obstacles. (Remove the call in OnAttach for a blank scene.)
        void BuildCharacterDemoScene();
        // Builds a walkable greybox house (walls + doorway + roof, static colliders) parented under one root.
        void BuildHouse( const glm::vec3& origin );

        void DrawEngineStats();
        void DrawProfilerWindow();
        /// The profiler's CPU+GPU table as log lines — the panel's button and --gpu-profile share it.
        void DumpProfilerToLog();

        // ===== Popups =====
        void DrawPopups();
        void DrawOpenScenePopup();
        // "Discard unsaved changes?" for a scene opened by drag-drop / double-click (see m_PendingOpenScene).
        void DrawConfirmOpenScenePopup();
        void DrawSaveScenePopup();
        void DrawNewScenePopup();
        void DrawReloadScenePopup();
        void DrawProjectPopup();

        void PrepareScenePopup();
        void LoadScene( const Common::Filepath& path );
        void LoadSceneInternal( const Common::Filepath& path );

        // Applies `--select <name-or-uuid>` once the scene has settled — see Editor/Core/StartupOptions.hpp
        // for why the flag exists at all. Runs exactly once per session; a name that matches nothing ends
        // the run non-zero rather than selecting nothing quietly.
        void ApplyStartupSelection();
        void NewSceneInternal(); // clears the current scene to a fresh empty one (File -> New Scene / Ctrl+N)

        // ===== Multi-scene editing (independent SceneRenderers) =====
        // Adds the standard ECS systems to a scene (shared by the main scene and any extra scene views).
        void BuildSceneSystems( Desert::Core::Scene& scene );
        // Opens a new, empty scene alongside the main one — its own SceneRenderer + RenderRegistry + a live
        // dockable viewport. Work on a UI/main-menu scene next to the game scene without switching.
        void AddSceneView();
        // Destroys the document named @p id: its viewport panel, render registry, scene and renderer, in that
        // order and behind a device-idle wait. Called from OnUpdate (between frames) when the user closes a
        // scene-view window; a no-op for an id that is already gone. This is what gives the renderer slot
        // back — see Engine/Core/RendererSlotPool.hpp.
        void CloseSceneView( uint64_t id );
        // Closes every scene view whose window the user dismissed since the last frame. One pass at the top
        // of OnUpdate, because a close destroys GPU resources and removes a panel from m_Panels — neither is
        // legal from inside the ImGui pass that is iterating it.
        void CloseDismissedSceneViews();

        // ===== Asset documents (one window per asset, opened from the browser) =====
        // Drains Core::AssetOpenRequests and, per request, focuses the document already open on that subject
        // or builds a new one through m_AssetEditors. Runs from OnUpdate (between frames) because it adds to
        // m_Documents, and REFUSES past the six renderer slots with the census printed by name — a seventh
        // consumer would otherwise be handed slot 0 to share, which fails silently and days later.
        void ServiceAssetOpenRequests();
        // Destroys every document the user asked to close, behind ONE device-idle wait. This is what returns
        // the document's Scene, SceneRenderer and renderer slot.
        //
        // The request comes from m_DocumentsToClose, filled by the x on the window, the x in the Documents
        // menu or Close All — never from a visibility flag. That is the point of the split: a tool's
        // visibility is a setting the user keeps, and while documents shared the panel list they shared that
        // bool too, so unticking one in the View menu DESTROYED it and re-ticking could not bring it back.
        void ServiceDocumentCloses();
        // Asks for a document to be closed. Queued, never immediate: closing destroys GPU resources, which
        // is not legal from inside the ImGui pass that is drawing them.
        void RequestDocumentClose( const Assets::AssetHandle& subject );
        // Brings @p subject's window to the front and makes it the most recently used document.
        void FocusDocument( const Assets::AssetHandle& subject );
        // Ctrl+Tab: move to the next document in most-recently-used order. See DocumentWell::NextMostRecent.
        void CycleDocuments();

        // ===== The document well (layout option B.1) =====
        // The permanent "Documents" window: the tab the documents dock beside, the index of what is open,
        // and — when nothing is open — the empty state that says what the area is for plus the list of
        // recently closed documents. It does not collapse when it empties: a layout that moves on its own is
        // what users report as "the editor lost my panel".
        void DrawDocumentWell();
        // Every open document, drawn into the well's dock node. Separate from the tool loop because the two
        // have separate owners and separate close semantics — a tool passes &GetVisibility() to Begin, a
        // document passes a frame-local bool whose false is a CLOSE REQUEST, not a hidden window.
        void DrawDocuments();
        // The refusal, on screen. A seventh renderer consumer is refused; before this the refusal was a
        // line in the log and the click simply looked dead. The census text already existed — it had
        // nowhere to be shown.
        void DrawOpenRefusedPopup();

        // Who is holding a renderer slot right now, by name. Printed when an open is refused — "no free
        // slot" without the list leaves the user with nothing to close. The main viewport and every extra
        // scene view hold one for as long as they exist; the Details preview and each asset document are
        // demand-driven and may be open while holding nothing.
        struct RendererSlotConsumer
        {
            std::string Name;
            bool        HoldsSlot = false;
            // Whether this consumer will ever want a slot. A CPU-only asset document (the four cloud
            // editors) holds none and is not waiting for one, and the census has to say so — "no slot right
            // now, but will claim one when it draws" would name it as something to close to free a slot it
            // was never going to take. See IAssetEditorPanel::ClaimsRendererSlot.
            bool ClaimsSlot = true;
            // Set for a consumer the user can close FROM THE REFUSAL ITSELF: an open document. A census that
            // names five things and offers no way to act on any of them is a longer version of "no free
            // slot". The main viewport and the Details preview carry no handle — neither is a window a
            // person closes to make room. Last in the struct so the two- and three-field aggregate
            // initialisations below keep meaning what they say.
            std::optional<Assets::AssetHandle> Document = {};
        };
        [[nodiscard]] std::vector<RendererSlotConsumer> RendererSlotCensus() const;
        // Rebinds the editor to a focused document: m_MainScene (and thus every play/save/gizmo call site)
        // points at it, Commands + the scene-bound panels follow. kPrimarySceneViewId = the primary/main
        // scene. An id whose document has been closed rebinds nothing and says so — see SceneViewIdentity.hpp
        // for why the viewports name their document instead of numbering it.
        void SetActiveScene( uint64_t id );
        // Runs one render frame for a scene (outline aid + Begin/RegistryRender/OnUpdate/End). Called for
        // every open document each frame so all viewports stay live.
        Common::BoolResultStr UpdateSceneFrame( Desert::Core::Scene& scene, Render::RenderRegistry* registry,
                                                const Common::Timestep& ts );

        // Startup content is DATA, not code — these build entities into m_MainScene so the result
        // can be serialized to a .desce ONCE and loaded like any scene afterwards.
        void BuildStarterScene();    // fresh Hub project's DefaultScene: sun/ground/cube/light/camera
        void BuildCornellShowcase(); // sandbox demo: baked into CornellDemo.desce on first launch
        // Serializes m_MainScene to @p path. False when the bytes did not land, with the reason logged;
        // the file that was there (if any) is unchanged. Both callers generate startup content, so a
        // false here means the project's own default scene is not on disk.
        [[nodiscard]] bool SaveSceneTo( const std::string& path );

        // THE ONE place the open scene is saved from. Every entry point (Ctrl+S, File -> Save, the
        // command palette, the "Save and Open" button) goes through it, so the policy — clear the
        // unsaved-changes mark and announce success ONLY when the bytes landed — is written once and
        // decided by a pure, tested rule (Editor/Core/SceneSaveRules.hpp). Returns whether the scene on
        // disk is now current; a caller about to destroy the in-memory scene MUST branch on it.
        [[nodiscard]] bool SaveOpenScene();

        // Force re-cook of Cooked/ from sources, re-register cooked assets, refresh the asset panel.
        void RebuildCookedAssets();

        // Read the resolved viewport back off the GPU and write it to @p path as a PNG, creating the
        // parent directory if it is missing. The single implementation behind the `--shot` still, every
        // frame of a `--shot-sequence`, and the F9 dump. False on any failure, always with the reason
        // logged and the numbers in it.
        bool WriteViewportPng( const std::string& path );

    private:
        enum class EditorState
        {
            Paused = 0,
            Play,
        };

        EditorState m_EditorState;
        std::string m_PlaySnapshot;        // serialized scene captured on Play, restored on Stop
        bool        m_ShowProfiler = true; // View ▸ Profiler toggles the profiler window

    private:
        const Engine::Application* m_Application;

        std::shared_ptr<Assets::AssetManager>        m_AssetManager;
        std::unique_ptr<Assets::AssetPreloader>      m_AssetPreloader;
        std::unique_ptr<ImportManager>               m_ImportManager;
        std::unique_ptr<Animation::AnimationLibrary> m_AnimationLibrary;
        Runtime::AssetHotReload                      m_AssetHotReload; // .demat/.shader live reload

        FileExplorerPanel* m_FileExplorerPanel = nullptr; // non-owning (lives in m_Panels)

        // m_MainScene is the ACTIVE document — rebound to the focused viewport's scene so the 100+ existing
        // call sites (play/save/gizmo/autosave) operate on it without change. m_PrimaryScene keeps a handle
        // to the original (index -1) so we can rebind back to it.
        std::shared_ptr<Desert::Core::Scene> m_MainScene;
        std::shared_ptr<Desert::Core::Scene> m_PrimaryScene;

        std::unique_ptr<Render::RenderRegistry> m_RenderRegistry;

        // Extra scenes opened alongside the main one (Scenes -> New Scene View). Each owns its own renderer,
        // editor render-registry and a live ViewportPanel (non-owning ptr; the panel lives in m_Panels).
        struct SceneDocument
        {
            // The document's name for as long as it exists, and the ONLY thing a viewport's activation
            // callback captures. Not its position: see Editor/Core/SceneViewIdentity.hpp for why an index
            // silently activates the wrong document the moment a view in front of it is closed.
            uint64_t                                Id = kPrimarySceneViewId;
            std::string                             Name;
            std::shared_ptr<Desert::Core::Scene>    Scene;
            std::unique_ptr<Graphic::SceneRenderer> Renderer;
            std::unique_ptr<Render::RenderRegistry> Registry;
            ViewportPanel*                          Viewport = nullptr;
        };
        std::vector<std::unique_ptr<SceneDocument>> m_ExtraScenes;
        SceneViewIdSource                           m_SceneViewIds;
        uint64_t m_ActiveSceneId = kPrimarySceneViewId; // which document the editor is bound to

        // AssetTypeID -> the editor that opens it. Holds factories only; the documents it builds are owned by
        // m_Documents below.
        AssetEditorRegistry m_AssetEditors;

        // THE OPEN DOCUMENTS, owned separately from the tools. See Editor/Core/DocumentWell.hpp for the
        // whole argument; the short version is that a tool's visibility is a setting and a document's
        // existence is not, so one bool cannot serve both — and while they shared m_Panels it had to.
        DocumentWell m_Documents;
        // Close requests, drained between frames by ServiceDocumentCloses. Filled by the x on a document
        // window, the x in Window ▸ Documents, Close All, and the refusal dialog's own Close buttons.
        std::vector<Assets::AssetHandle> m_DocumentsToClose;
        // Which document window has the keyboard focus, as of the last frame. Drives the radio in
        // Window ▸ Documents and is where Ctrl+Tab starts from.
        Assets::AssetHandle m_FocusedDocument = Common::UUID::Null();
        // Ctrl+Tab holds the ring still. Landing on a document by cycling must NOT reorder the ring, or the
        // second press would come straight back to where the first started; the order is committed once Ctrl
        // is released, which is the behaviour every alt-tab ring has.
        bool m_CyclingDocuments = false;
        // The dock node the documents live in (layout option B.1): the centre column is split, the level
        // keeps the left node, documents get the right one. Read back from the well window's own dock id
        // every frame rather than remembered from the one frame the layout was built — a value captured at
        // build time is 0 for the whole of every later session.
        ImGuiID m_DocumentDockId = 0;

        // A refused open, waiting to be shown (see DrawOpenRefusedPopup). Holds the census by value: the
        // documents it names may be closed while the dialog is up, and a row pointing at a destroyed panel
        // is the dangling reference this split exists to avoid.
        struct OpenRefusal
        {
            std::string                       AssetName;
            std::string                       TypeName;
            uint32_t                          Live    = 0;
            uint32_t                          Pending = 0;
            std::vector<RendererSlotConsumer> Census;
        };
        std::optional<OpenRefusal> m_OpenRefusal;
        bool                       m_OpenRefusalPending = false; // raise the modal on the next ImGui frame

#ifdef EBABLE_IMGUI
        std::shared_ptr<ImGui::ImGuiLayer> m_ImGuiLayer;
        // THE TOOLS. A container that cannot hold a document — see Editor/Core/PanelRegistry.hpp. That is
        // what makes "the View menu lists exactly the tools" true by construction rather than by a predicate
        // the menu, the command palette and --open-panel would each have had to remember.
        PanelRegistry m_Panels;

        // Contextual panels (IPanel::IsContextual): which ones WE opened, so a panel the user opened by
        // hand is never auto-closed, and the one to bring to the front of its dock this frame.
        std::unordered_set<Editor::IPanel*> m_ContextualShown;
        std::string                         m_FocusPanel;

        CommandPalette m_CommandPalette;

        // Crash recovery: set at startup when the previous session crashed and an autosave was found.
        bool                  m_ShowRecoveryPrompt = false;
        std::filesystem::path m_RecoveryAutosave;

        // Saveable layouts: pending "reset to default docking" and the save-layout modal state.
        bool m_ResetDefaultLayout  = false;
        bool m_ShowSaveLayoutPopup = false;

        // Bottom drawer (Assets / Logs / Shader Code). Collapsing SHRINKS the dock node to its tab bar
        // instead of closing the panels: a closed panel has to be rediscovered from a menu, a collapsed
        // one is still right there. m_BottomHeight remembers the expanded size across toggles.
        ImGuiID m_BottomDockId    = 0;
        bool    m_BottomCollapsed = false;
        float   m_BottomHeight    = 0.0f;
        void    DrawBottomDrawerToggle();
        char m_LayoutNameBuf[64]   = {};
#endif
        std::unique_ptr<Graphic::SceneRenderer> m_SceneRenderer;
        bool                                    m_OpenScenePopup        = false;
        bool                                    m_SaveSceneRequested    = false;
        // Set when "Save and Open" could not write the scene: the modal STAYS OPEN and shows this, so
        // the choice the user is making ("throw this scene away") is made knowing the save did not
        // happen. Cleared whenever the modal is dismissed.
        std::string                             m_SaveAndOpenError;
        bool                                    m_NewSceneRequested     = false;
        bool                                    m_AddSceneViewRequested = false; // Scenes -> New Scene View

        // Staged startup loading (UI loader): the heavy boot work (mesh cooking, asset preload) runs one
        // stage per frame from OnUpdate while OnImGuiRender shows a fullscreen progress overlay — instead
        // of silently freezing the window for seconds before the first frame.
        struct StartupStage
        {
            std::string           Label;
            std::function<void()> Run;
        };
        std::vector<StartupStage> m_StartupStages;
        size_t                    m_StartupNext           = 0;
        int                       m_StartupFramesRendered = 0;
        bool                      StartupLoading() const
        {
            return m_StartupNext < m_StartupStages.size();
        }
        // Screenshot mode counters (see Editor/Core/ShotOptions.hpp).
        int  m_ShotFrame        = 0;
        bool m_ShotCameraPlaced = false;
        // Set when any PNG of this capture could not be written; becomes the process exit status.
        bool m_ShotFailed = false;

        // `--select` is applied once, on the first frame where no scene load is outstanding — whether the
        // scene came from --scene, from the project's default, or nowhere at all. A latch rather than a
        // check against the flag's own value, because the flag stays set for the life of the process and
        // re-applying it every frame would fight a person clicking in the outliner.
        bool m_StartupSelectionApplied = false;

        std::optional<Common::Filepath> m_SceneLoadRequested;
        // Stop tears down + recreates GPU render resources (framebuffers / render graph). It must run
        // BETWEEN frames (like a scene load), never inline in the ImGui Stop-button handler — otherwise the
        // next frame begins a render pass against a just-destroyed framebuffer (driver access violation in
        // vkCmdBeginRenderPass). Deferred to the top of OnUpdate.
        bool                          m_PendingSceneStop = false;
        std::vector<Common::Filepath> m_AvailableScenes;
        std::vector<Common::Filepath> m_RecentScenes;
        int                           m_SelectedSceneIndex = -1;
        // Open Scene popup: substring filter over the (recursive) scene list — with subfolders the list is
        // long enough that scrolling for a name is worse than typing it.
        char m_SceneFilter[128] = {};

        // A scene a panel asked to open (dropped on the viewport, double-clicked in the browser) while the
        // current one had unsaved edits: held until the confirm popup says discard/save/cancel.
        std::optional<Common::Filepath> m_PendingOpenScene;
        bool                            m_ConfirmOpenScenePopup = false;
    };
} // namespace Desert::Editor