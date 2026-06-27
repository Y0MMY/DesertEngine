#pragma once

#include <Engine/Desert.hpp>
#include <Engine/Assets/AssetManager.hpp>

#include "../IPanel.hpp"

namespace Desert::Editor
{
    class SceneHierarchyPanel final : public IPanel
    {
    public:
        explicit SceneHierarchyPanel( const std::shared_ptr<Desert::Core::Scene>&  scene,
                                      const std::shared_ptr<Assets::AssetManager>& assetManager )
             : IPanel( "Scene Outliner" ), m_Scene( scene ), m_AssetManager( assetManager )
        {
        }
        void OnUIRender() override;

    private:
        static const char* GetEntityTypeName( const ECS::Entity& entity );
        void               DrawEntityNode( ECS::Entity& entity );
        void               DrawInstantiatePrefabPopup();

    private:
        const std::shared_ptr<Desert::Core::Scene>  m_Scene;
        const std::shared_ptr<Assets::AssetManager> m_AssetManager;
        ImGuiTextFilter                             m_HierarchyFilter;
        std::string                                 m_PrefabInstantiatePath =
             "Resources/Assets/Prefabs/MyPrefab.deprefab";
        bool                                        m_OpenInstantiatePrefab = false; // deferred OpenPopup
        std::optional<Common::UUID>                 m_PendingDelete; // deferred: destroy after UI iteration
    };
} // namespace Desert::Editor