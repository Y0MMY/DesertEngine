#pragma once

#include <Engine/Desert.hpp>

#include "MaterialsPanel.hpp"
#include "../IPanel.hpp"

namespace Desert::Editor
{
    class ScenePropertiesPanel final : public IPanel
    {
    public:
        explicit ScenePropertiesPanel( const std::shared_ptr<Desert::Core::Scene>&       scene,
                                       const std::shared_ptr<Assets::AssetManager>&      assetManager,
                                       const std::shared_ptr<Runtime::ResourceRegistry>& resourceRegistry )
             : IPanel( "Scene Properties" ), m_Scene( scene ), m_AssetManager( assetManager ),
               m_ResourceRegistry( resourceRegistry ),
               m_MaterialsPanel( std::make_shared<MaterialsPanel>( resourceRegistry ) )
        {
        }
        void OnUIRender() override;

    private:
        void DrawMaterialEntity( const ECS::Entity& entity );

    private:
        std::shared_ptr<Desert::Core::Scene>           m_Scene;
        const std::shared_ptr<Assets::AssetManager>    m_AssetManager;
        const std::weak_ptr<Runtime::ResourceRegistry> m_ResourceRegistry;
        const std::shared_ptr<MaterialsPanel>          m_MaterialsPanel;

        bool m_DebugMode = false;
    };
} // namespace Desert::Editor