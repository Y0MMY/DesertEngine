#include "PointLightComponent.hpp"

#include <Editor/Panels/PropertyEditor/PropertyEditorBuilder.hpp>

namespace Desert::Editor
{
    PointLightComponentWidget::PointLightComponentWidget() : ComponentWidget( "Point Light" )
    {
    }

    void PointLightComponentWidget::Render( ECS::Entity& entity, ::Desert::Core::Scene* scene )
    {
        // Fully reflection-driven UI from PointLightData's PROPERTY() metadata.
        auto& pointLight = entity.GetComponent<ECS::PointLightComponent>();
        PropertyEditorBuilder::Draw( &pointLight.Data, "PointLightData" );
    }

} // namespace Desert::Editor