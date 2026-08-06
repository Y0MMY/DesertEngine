#pragma once

#include <Engine/Desert.hpp>
#include <Engine/Runtime/AssetHotReload.hpp>
#include <imgui/imgui.h>
#include "Editor/Widgets/UIHelper/ImGuiUI.hpp"
#include "Editor/Panels/IPanel.hpp"
#include "Editor/Core/CommandPalette.hpp"
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

        // UE5-style toolbar strip below the menu bar: transform-gizmo mode toggles (left) + Play/Pause/Stop
        // (centre). Drawn inside the dockspace host window so it takes a fixed height above the docked panels.
        void DrawToolbar();

        // Bottom status bar: scene state (Edit/Play), scene name, current selection, and FPS/frame time.
        void DrawStatusBar();
        // The status bar's console line (UE's "Enter Console Command"); handed to the Lua console to run.
        char m_StatusCmd[256] = {};
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

        // ===== Popups =====
        void DrawPopups();
        void DrawOpenScenePopup();
        void DrawSaveScenePopup();
        void DrawNewScenePopup();
        void DrawReloadScenePopup();
        void DrawProjectPopup();

        void PrepareScenePopup();
        void LoadScene( const Common::Filepath& path );
        void LoadSceneInternal( const Common::Filepath& path );
        void NewSceneInternal(); // clears the current scene to a fresh empty one (File -> New Scene / Ctrl+N)

        // ===== Multi-scene editing (independent SceneRenderers) =====
        // Adds the standard ECS systems to a scene (shared by the main scene and any extra scene views).
        void BuildSceneSystems( Desert::Core::Scene& scene );
        // Opens a new, empty scene alongside the main one — its own SceneRenderer + RenderRegistry + a live
        // dockable viewport. Work on a UI/main-menu scene next to the game scene without switching.
        void AddSceneView();
        // Rebinds the editor to a focused document: m_MainScene (and thus every play/save/gizmo call site)
        // points at it, Commands + the scene-bound panels follow. index < 0 = the primary/main scene.
        void SetActiveScene( int index );
        // Runs one render frame for a scene (outline aid + Begin/RegistryRender/OnUpdate/End). Called for
        // every open document each frame so all viewports stay live.
        Common::BoolResultStr UpdateSceneFrame( Desert::Core::Scene& scene, Render::RenderRegistry* registry,
                                                const Common::Timestep& ts );

        // Startup content is DATA, not code — these build entities into m_MainScene so the result
        // can be serialized to a .desce ONCE and loaded like any scene afterwards.
        void BuildStarterScene();    // fresh Hub project's DefaultScene: sun/ground/cube/light/camera
        void BuildCornellShowcase(); // sandbox demo: baked into CornellDemo.desce on first launch
        void SaveSceneTo( const std::string& path );

        // Force re-cook of Cooked/ from sources, re-register cooked assets, refresh the asset panel.
        void RebuildCookedAssets();

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
            std::string                             Name;
            std::shared_ptr<Desert::Core::Scene>    Scene;
            std::unique_ptr<Graphic::SceneRenderer> Renderer;
            std::unique_ptr<Render::RenderRegistry> Registry;
            ViewportPanel*                          Viewport = nullptr;
        };
        std::vector<std::unique_ptr<SceneDocument>> m_ExtraScenes;
        int                                         m_ActiveSceneIndex = -1; // -1 = primary; else m_ExtraScenes[i]

#ifdef EBABLE_IMGUI
        std::shared_ptr<ImGui::ImGuiLayer>           m_ImGuiLayer;
        std::vector<std::unique_ptr<Editor::IPanel>> m_Panels;

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
        std::optional<Common::Filepath> m_SceneLoadRequested;
        // Stop tears down + recreates GPU render resources (framebuffers / render graph). It must run
        // BETWEEN frames (like a scene load), never inline in the ImGui Stop-button handler — otherwise the
        // next frame begins a render pass against a just-destroyed framebuffer (driver access violation in
        // vkCmdBeginRenderPass). Deferred to the top of OnUpdate.
        bool                          m_PendingSceneStop = false;
        std::vector<Common::Filepath> m_AvailableScenes;
        std::vector<Common::Filepath> m_RecentScenes;
        int                           m_SelectedSceneIndex = -1;
    };
} // namespace Desert::Editor