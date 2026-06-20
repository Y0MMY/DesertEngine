#pragma once

#include <string>

#include <ImGui/imgui.h>

#include <Common/Core/Events/Event.hpp>

namespace Desert::Editor
{
    class IPanel
    {
    public:
        explicit IPanel( std::string&& panelName, bool showPanel = true )
             : m_PanelName( std::move( panelName ) ), m_SowPanel( showPanel )
        {
        }

        virtual void OnEvent( Common::Event& e ) {}
        virtual ~IPanel()                        = default;
        virtual void       OnUIRender()          = 0;
        const std::string& GetName() const
        {
            return m_PanelName;
        }

        virtual void ToggleVisibility() final
        {
            m_SowPanel = !m_SowPanel;
        }

        virtual bool& GetVisibility()
        {
            return m_SowPanel;
        }

    protected:
        const std::string m_PanelName;
        bool              m_SowPanel;
    };
} // namespace Desert::Editor