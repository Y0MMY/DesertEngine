#include "DirectionalLightComponentWidget.hpp"

#include <Editor/Panels/PropertyEditor/PropertyEditorBuilder.hpp>

namespace Desert::Editor
{
    DirectionalLightComponentWidget::DirectionalLightComponentWidget() : ComponentWidget( "Directional Light" )
    {
    }

    void DirectionalLightComponentWidget::Render( ECS::Entity& entity, ::Desert::Core::Scene* scene )
    {
        // Direction comes from the entity transform; Color/Intensity are reflection-driven here.
        auto& light = entity.GetComponent<ECS::DirectionLightComponent>();
        PropertyEditorBuilder::Draw( &light.Data, "DirectionalLightData" );
    }
} // namespace Desert::Editor
