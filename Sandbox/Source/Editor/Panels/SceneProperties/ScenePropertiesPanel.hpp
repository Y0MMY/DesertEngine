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
                                       const std::shared_ptr<Runtime::ResourceResolver>& resolver )
             : IPanel( "Scene Properties" ), m_Scene( scene ), m_AssetManager( assetManager ),
               m_RuntimeResourceResolver( resolver ),
               m_MaterialsPanel( std::make_shared<MaterialsPanel>( resolver ) )
        {
        }
        void OnUIRender() override;

    private:
        void DrawMaterialEntity( const ECS::Entity& entity );

    private:
        std::shared_ptr<Desert::Core::Scene>           m_Scene;
        const std::shared_ptr<Assets::AssetManager>    m_AssetManager;
        const std::weak_ptr<Runtime::ResourceResolver> m_RuntimeResourceResolver;
        const std::shared_ptr<MaterialsPanel>          m_MaterialsPanel;
    };
} // namespace Desert::Editor