#pragma once

#include <Engine/Desert.hpp>

#include "../IPanel.hpp"

namespace Desert::Editor
{
    class ScenePropertiesPanel final : public IPanel
    {
    public:
        explicit ScenePropertiesPanel() : IPanel( "Scene Properties" )
        {
        }
        void OnUIRender() override;
    };
} // namespace Desert::Editor