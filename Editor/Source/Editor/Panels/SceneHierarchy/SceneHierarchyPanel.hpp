#pragma once

#include <Engine/Desert.hpp>

#include "../IPanel.hpp"

namespace Desert::Editor
{
    class SceneHierarchyPanel final : public IPanel
    {
    public:
        explicit SceneHierarchyPanel( const std::shared_ptr<Desert::Core::Scene>& scene )
             : IPanel( "Scene Hierarchy" ), m_Scene( scene )
        {
        }
        void OnUIRender() override;

    private:

        void DrawEntityNode( ECS::Entity& entity );
    private:
        const std::shared_ptr<Desert::Core::Scene> m_Scene;
        ImGuiTextFilter m_HierarchyFilter;
    };
} // namespace Desert::Editor