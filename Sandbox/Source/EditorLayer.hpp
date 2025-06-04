#pragma once

#include <Engine/Desert.hpp>
#include <imgui/imgui.h>
#include "Editor/Widgets/UIHelper/ImGuiUI.hpp"
#include "Editor/Panels/IPanel.hpp"

namespace Desert
{
    class EditorLayer : public Common::Layer, public Common::EventHandler
    {
    public:
        explicit EditorLayer( const std::shared_ptr<Common::Window>& window, const std::string& layerName );
        ~EditorLayer();

        [[nodiscard]] virtual Common::BoolResult OnAttach() override;
        [[nodiscard]] virtual Common::BoolResult OnDetach() override;
        [[nodiscard]] virtual Common::BoolResult OnUpdate( Common::Timestep ts ) override;
        [[nodiscard]] virtual Common::BoolResult OnImGuiRender() override;

        void OnEvent( Common::Event& e ) override;

    private:
        bool OnWindowResize( Common::EventWindowResize& e );

    private:
        const std::shared_ptr<Common::Window>   m_Window;
        std::shared_ptr<Core::Scene>            m_testscene;
        std::shared_ptr<Graphic::SceneRenderer> m_testscenerenderer;
        std::shared_ptr<Graphic::TextureCube>   m_Skybox;
        std::shared_ptr<Mesh>                   m_Mesh;

        Core::Camera m_EditorCamera;

        ImVec2 m_Size;

#ifdef EBABLE_IMGUI
        std::shared_ptr<ImGui::ImGuiLayer>           m_ImGuiLayer;
        std::unique_ptr<Editor::UI::UIHelper>        m_UIHelper;
        std::vector<std::unique_ptr<Editor::IPanel>> m_Panels;
#endif // EBABLE_IMGUI
    };
} // namespace Desert