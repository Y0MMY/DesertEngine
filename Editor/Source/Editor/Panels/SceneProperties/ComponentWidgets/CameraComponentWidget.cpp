#include "CameraComponentWidget.hpp"

#include <Editor/Panels/PropertyEditor/PropertyEditorBuilder.hpp>

namespace Desert::Editor
{
    CameraComponentWidget::CameraComponentWidget() : ComponentWidget( "Camera" )
    {
    }

    void CameraComponentWidget::Render( ECS::Entity& entity, ::Desert::Core::Scene* scene )
    {
        auto& camera = entity.GetComponent<ECS::CameraComponent>();
        PropertyEditorBuilder::Draw( &camera.Data, "CameraData" );
    }
} // namespace Desert::Editor
