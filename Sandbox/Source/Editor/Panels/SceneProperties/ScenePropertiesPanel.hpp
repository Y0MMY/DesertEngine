#pragma once

#include <Engine/Desert.hpp>

#include "MaterialsPanel.hpp"
#include "../IPanel.hpp"

namespace Desert::Editor
{
    class ScenePropertiesPanel final : public IPanel
    {
    public:
        explicit ScenePropertiesPanel(
             const std::shared_ptr<Desert::Core::Scene>&             scene,
             const std::shared_ptr<Runtime::RuntimeResourceManager>& runtimeResourceManager )
             : IPanel( "Scene Properties" ), m_Scene( scene ), m_RuntimeResourceManager( runtimeResourceManager ),
               m_MaterialsPanel( std::make_shared<MaterialsPanel>( m_RuntimeResourceManager ) )
        {
        }
        void OnUIRender() override;

    private:
        void DrawMaterialEntity( const ECS::Entity& entity );

    private:
        std::shared_ptr<Desert::Core::Scene>                   m_Scene;
        const std::shared_ptr<Runtime::RuntimeResourceManager> m_RuntimeResourceManager;
        const std::shared_ptr<MaterialsPanel>                  m_MaterialsPanel;
    };
} // namespace Desert::Editor