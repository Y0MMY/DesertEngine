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
        [[nodiscard]] virtual Common::BoolResult OnUpdate( const Common::Timestep& ts ) override;
        [[nodiscard]] virtual Common::BoolResult OnImGuiRender() override;

        void OnEvent( Common::Event& e ) override;

    private:
        bool OnWindowResize( Common::EventWindowResize& e );
        bool OnMousePressed( Common::MouseButtonPressedEvent& e );
        bool OnKeyPressedEvent( Common::KeyPressedEvent& e );

        void HandleObjectPicking();

        std::pair<float, float> GetMouseViewportSpace();

    private:
        enum class GizmoType
        {
            None      = -1,
            Translate = 7,   // ImGuizmo::OPERATION::TRANSLATE
            Rotate    = 120, // ImGuizmo::OPERATION::ROTATE
            Scale     = 896, // ImGuizmo::OPERATION::SCALE
        };

        GizmoType m_GizmoType = GizmoType::None;
        bool m_GizmoHovered = false;

    private:
        const std::shared_ptr<Common::Window> m_Window;
        std::shared_ptr<Core::Scene>          m_MainScene;
        Graphic::Environment                  m_Environment;
        Core::Camera                          m_EditorCamera;

        struct ViewportData
        {
            glm::vec2 MousePosition;
            glm::vec2 Size;
            glm::vec2 Bounds[2];
            bool      IsHovered = false;
            float     DpiScale  = 1.0f;
        };

        ViewportData m_ViewportData;

        std::shared_ptr<Assets::AssetManager>      m_AssetManager;
        std::unique_ptr<Assets::AssetPreloader>    m_AssetPreloader;
        std::shared_ptr<Runtime::ResourceRegistry> m_ResourceRegistry;

#ifdef EBABLE_IMGUI
        std::shared_ptr<ImGui::ImGuiLayer>           m_ImGuiLayer;
        std::unique_ptr<Editor::UI::UIHelper>        m_UIHelper;
        std::vector<std::unique_ptr<Editor::IPanel>> m_Panels;
#endif // EBABLE_IMGUI
    };
} // namespace Desert