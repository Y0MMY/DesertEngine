#pragma once

#include <Engine/Desert.hpp>

#include "../IPanel.hpp"

namespace Desert::Editor
{
    class SceneHierarchyPanel final : public IPanel
    {
    public:
        explicit SceneHierarchyPanel( const std::shared_ptr<Core::Scene>& scene )
             : IPanel( "Scene Hierarchy" ), m_Scene( scene )
        {
        }
        void OnUIRender() const override;

    private:
        const std::shared_ptr<Core::Scene> m_Scene;
    };
} // namespace Desert::Editor