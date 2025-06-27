#pragma once

#include <Engine/Desert.hpp>

#include "MaterialsPanel.hpp"
#include "../IPanel.hpp"

namespace Desert::Editor
{
    class ScenePropertiesPanel final : public IPanel
    {
    public:
        explicit ScenePropertiesPanel( const std::shared_ptr<Desert::Core::Scene>&  scene,
                                       const std::shared_ptr<Assets::AssetManager>& assetManager )
             : IPanel( "Scene Properties" ), m_Scene( scene ), m_AssetManager( assetManager ),
               m_MaterialsPanel( std::make_shared<MaterialsPanel>( m_AssetManager ) )
        {
        }
        void OnUIRender() override;

    private:
        void DrawMaterialEditor( Assets::Asset<Assets::MaterialAsset>& material );
        void DrawMaterialEntity( const ECS::Entity& entity );

        void DrawMaterialInfo( std::shared_ptr<Graphic::MaterialInstance>& materialInstance );

    private:
        std::shared_ptr<Desert::Core::Scene>        m_Scene;
        const std::shared_ptr<Assets::AssetManager> m_AssetManager;
        const std::shared_ptr<MaterialsPanel>       m_MaterialsPanel;
    };
} // namespace Desert::Editor