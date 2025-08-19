#pragma once

#include <Engine/Desert.hpp>

#include "Editor/Panels/IPanel.hpp"

#include "Editor/Widgets/UIHelper/ImGuiUI.hpp"

namespace Desert::Editor
{
    class ViewportPanel : public IPanel, public Common::EventHandler
    {
    public:
        explicit ViewportPanel( const std::shared_ptr<Desert::Core::Scene>&       scene,
                                const std::shared_ptr<Runtime::ResourceRegistry>& resourceRegistry );
        void OnUIRender() override;

        void OnEvent( Common::Event& e ) override;

    private:
        bool OnWindowResize( Common::EventWindowResize& e );
        bool OnMousePressed( Common::MouseButtonPressedEvent& e );
        bool OnKeyPressedEvent( Common::KeyPressedEvent& e );

    private:
        // Gizmo functionality
        enum class GizmoType
        {
            None      = -1,
            Translate = 7,   // ImGuizmo::OPERATION::TRANSLATE
            Rotate    = 120, // ImGuizmo::OPERATION::ROTATE
            Scale     = 896, // ImGuizmo::OPERATION::SCALE
        };
        void SetGizmoType( GizmoType type )
        {
            m_GizmoType = type;
        }

        // Viewport data access
        const glm::vec2& GetSize() const
        {
            return m_ViewportData.Size;
        }
        bool IsHovered() const
        {
            return m_ViewportData.IsHovered;
        }

    private:
        void                    HandleObjectPicking();
        std::pair<float, float> GetMouseViewportSpace() const;
        void                    RenderGizmo();

        struct ViewportData
        {
            glm::vec2 Size          = { 0.0f, 0.0f };
            glm::vec2 MousePosition = { 0.0f, 0.0f };
            bool      IsHovered     = false;
        };

        ViewportData m_ViewportData;
        GizmoType    m_GizmoType    = GizmoType::None;
        bool         m_GizmoHovered = false;

        std::shared_ptr<Desert::Core::Scene>       m_Scene;
        std::shared_ptr<Runtime::ResourceRegistry> m_ResourceRegistry;
        std::unique_ptr<Editor::UI::UIHelper>      m_UIHelper;
    };
} // namespace Desert::Editor