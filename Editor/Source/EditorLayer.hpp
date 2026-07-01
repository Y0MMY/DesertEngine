#pragma once

#include <Engine/Desert.hpp>
#include <imgui/imgui.h>
#include "Editor/Widgets/UIHelper/ImGuiUI.hpp"
#include "Editor/Panels/IPanel.hpp"
#include "Editor/RenderSystems/RenderRigistry.hpp"

namespace Desert::Editor
{
    class ImportManager;
    class FileExplorerPanel;

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

        // ===== Top Bar Sections =====
        void DrawProjectSection();
        void DrawSceneRenameSection();
        void DrawPlaybackControls();
        void DrawPlayButton();
        void DrawPauseButton();

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

        // Force re-cook of Cooked/ from sources, re-register cooked assets, refresh the asset panel.
        void RebuildCookedAssets();

    private:
        enum class EditorState
        {
            Paused = 0,
            Play,
        };

        EditorState m_EditorState;
        std::string m_PlaySnapshot; // serialized scene captured on Play, restored on Stop
        bool        m_ShowProfiler = true; // View ▸ Profiler toggles the profiler window

    private:
        const Engine::Application* m_Application;

        std::shared_ptr<Assets::AssetManager>        m_AssetManager;
        std::unique_ptr<Assets::AssetPreloader>      m_AssetPreloader;
        std::unique_ptr<ImportManager>               m_ImportManager;
        std::unique_ptr<Animation::AnimationLibrary> m_AnimationLibrary;

        FileExplorerPanel* m_FileExplorerPanel = nullptr; // non-owning (lives in m_Panels)

        std::shared_ptr<Desert::Core::Scene> m_MainScene;

        std::unique_ptr<Render::RenderRegistry> m_RenderRegistry;

#ifdef EBABLE_IMGUI
        std::shared_ptr<ImGui::ImGuiLayer>           m_ImGuiLayer;
        std::vector<std::unique_ptr<Editor::IPanel>> m_Panels;
#endif
        std::unique_ptr<Graphic::SceneRenderer> m_SceneRenderer;
        bool                                    m_OpenScenePopup     = false;
        bool                                    m_SaveSceneRequested = false;
        std::optional<Common::Filepath>         m_SceneLoadRequested;
        // Stop tears down + recreates GPU render resources (framebuffers / render graph). It must run
        // BETWEEN frames (like a scene load), never inline in the ImGui Stop-button handler — otherwise the
        // next frame begins a render pass against a just-destroyed framebuffer (driver access violation in
        // vkCmdBeginRenderPass). Deferred to the top of OnUpdate.
        bool                                    m_PendingSceneStop = false;
        std::vector<Common::Filepath>           m_AvailableScenes;
        std::vector<Common::Filepath>           m_RecentScenes;
        int                                     m_SelectedSceneIndex = -1;
    };
} // namespace Desert::Editor