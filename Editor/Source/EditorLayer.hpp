#pragma once

#include <Engine/Desert.hpp>
#include <imgui/imgui.h>
#include "Editor/Widgets/UIHelper/ImGuiUI.hpp"
#include "Editor/Panels/IPanel.hpp"
#include "Editor/RenderSystems/RenderRigistry.hpp"

namespace Desert::Editor
{
    class EditorLayer : public Common::Layer
    {
    public:
        explicit EditorLayer( const Engine::Application* window, const std::string& layerName );
        ~EditorLayer();

        [[nodiscard]] virtual Common::BoolResultStr OnAttach() override;
        [[nodiscard]] virtual Common::BoolResultStr OnDetach() override;
        [[nodiscard]] virtual Common::BoolResultStr OnUpdate( const Common::Timestep& ts ) override;
        [[nodiscard]] virtual Common::BoolResultStr OnImGuiRender() override;

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
        void DrawEngineStats();

        // ===== Popups =====
        void DrawPopups();
        void DrawOpenScenePopup();
        void DrawSaveScenePopup();
        void DrawNewScenePopup();
        void DrawReloadScenePopup();
        void DrawProjectPopup();

        void PrepareScenePopup();

    private:
        enum class EditorState
        {
            Paused = 0,
            Play,
        };

        EditorState m_EditorState;

    private:
        const Engine::Application* m_Application;

        std::shared_ptr<Assets::AssetManager>        m_AssetManager;
        std::unique_ptr<Assets::AssetPreloader>      m_AssetPreloader;
        std::unique_ptr<Animation::AnimationLibrary> m_AnimationLibrary;

        std::shared_ptr<Desert::Core::Scene> m_MainScene;

        std::unique_ptr<Render::RenderRegistry> m_RenderRegistry;

#ifdef EBABLE_IMGUI
        std::shared_ptr<ImGui::ImGuiLayer>           m_ImGuiLayer;
        std::vector<std::unique_ptr<Editor::IPanel>> m_Panels;
#endif
        std::unique_ptr<Graphic::SceneRenderer> m_SceneRenderer;
        bool                                    m_OpenScenePopup     = false;
        bool                                    m_SaveSceneRequested = false;
        std::vector<Common::Filepath>           m_AvailableScenes;
        int                                     m_SelectedSceneIndex = -1;
    };
} // namespace Desert::Editor