#pragma once

#include <Engine/Desert.hpp>

#include "../IPanel.hpp"

namespace Desert::Editor
{
    class SceneHierarchyPanel final : public IPanel
    {
    public:
        explicit SceneHierarchyPanel( const std::shared_ptr<Desert::Core::Scene>&  scene,
                                      const std::shared_ptr<Assets::AssetManager>& assetManager )
             : IPanel( "Scene Hierarchy" ), m_Scene( scene ), m_AssetManager( assetManager )
        {
        }
        void OnUIRender() override;

    private:
        void AddComponent( const ECS::Entity& entity );

    private:
        const std::shared_ptr<Desert::Core::Scene>  m_Scene;
        const std::shared_ptr<Assets::AssetManager> m_AssetManager;
    };
} // namespace Desert::Editor