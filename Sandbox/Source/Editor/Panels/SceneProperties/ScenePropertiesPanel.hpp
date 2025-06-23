#pragma once

#include <Engine/Desert.hpp>

#include "../IPanel.hpp"

namespace Desert::Editor
{
    class ScenePropertiesPanel final : public IPanel
    {
    public:
        explicit ScenePropertiesPanel( const std::shared_ptr<Desert::Core::Scene>&  scene,
                                       const std::shared_ptr<Assets::AssetManager>& assetManager )
             : IPanel( "Scene Properties" ), m_Scene( scene ), m_AssetManager( assetManager )
        {
        }
        void OnUIRender() override;

    private:
        void DrawMaterialEditor( Assets::Asset<Assets::MaterialAsset>& material );
        void DrawMaterialEntity( const ECS::Entity& entity );

    private:
        std::shared_ptr<Desert::Core::Scene>        m_Scene;
        const std::shared_ptr<Assets::AssetManager> m_AssetManager;
    };
} // namespace Desert::Editor