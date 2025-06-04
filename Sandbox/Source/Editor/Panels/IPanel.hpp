#pragma once

#include <string>

namespace Desert::Editor
{
    class IPanel
    {
    public:
        explicit IPanel( std::string&& panelName ) : m_PanelName( std::move( panelName ) )
        {
        }

        virtual ~IPanel()               = default;
        virtual void       OnUIRender() = 0;
        const std::string& GetName() const
        {
            return m_PanelName;
        }

    protected:
        const std::string m_PanelName;
    };
} // namespace Desert::Editor