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

        [[nodiscard]] virtual Common::BoolResult OnAttach() override;
        [[nodiscard]] virtual Common::BoolResult OnDetach() override;
        [[nodiscard]] virtual Common::BoolResult OnUpdate( const Common::Timestep& ts ) override;
        [[nodiscard]] virtual Common::BoolResult OnImGuiRender() override;

    private:
        void DrawMenuBar();

    private:
        enum class EditorState
        {
            Paused = 0,
            Play,
        };

        EditorState m_EditorState;

    private:
        const Engine::Application*           m_Application;
        std::shared_ptr<Desert::Core::Scene> m_MainScene;

        std::shared_ptr<Assets::AssetManager>      m_AssetManager;
        std::unique_ptr<Assets::AssetPreloader>    m_AssetPreloader;
        std::unique_ptr<Render::RenderRegistry>    m_RenderRegistry;

#ifdef EBABLE_IMGUI
        std::shared_ptr<ImGui::ImGuiLayer>           m_ImGuiLayer;
        std::vector<std::unique_ptr<Editor::IPanel>> m_Panels;
#endif // EBABLE_IMGUI
    };
} // namespace Desert::Editor