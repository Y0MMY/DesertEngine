#pragma once

#include <Engine/Desert.hpp>

#include "../IPanel.hpp"

namespace Desert::Editor
{
    class ScenePropertiesPanel final : public IPanel
    {
    public:
        explicit ScenePropertiesPanel( const std::shared_ptr<Desert::Core::Scene>& scene )
             : IPanel( "Scene Properties" ), m_Scene( scene )
        {
        }
        void OnUIRender() override;

    private:
        std::shared_ptr<Desert::Core::Scene> m_Scene;
    };
} // namespace Desert::Editor