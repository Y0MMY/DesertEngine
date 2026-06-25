#include "TransformComponentWidget.hpp"
#include <Editor/Widgets/Controls/Controls.hpp>
#include <Editor/Panels/PropertyEditor/ComponentWidgetRegistry.hpp>

#include <ImGui/imgui.h>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    TransformComponentWidget::TransformComponentWidget() : ComponentWidget( "Transform" )
    {
    }

    void TransformComponentWidget::Render( ECS::Entity& entity, ::Desert::Core::Scene* scene )
    {
        if ( !entity.HasComponent<ECS::DirectionLightComponent>() )
        {
            ImGui::Dummy( ImVec2( 0, 4 ) );
            auto& transform = entity.GetComponent<ECS::TransformComponent>();

            Widgets::DrawVec3Control( "Position", transform.Translation );
            ImGui::Dummy( ImVec2( 0, 6 ) );
            Widgets::DrawVec3Control( "Rotation", transform.Rotation );
            ImGui::Dummy( ImVec2( 0, 6 ) );
            Widgets::DrawVec3Control( "Scale", transform.Scale, 1.0f );
        }
        else
        {
            ImGui::Dummy( ImVec2( 0, 4 ) );
            auto& transform = entity.GetComponent<ECS::TransformComponent>();
            Widgets::DrawDirectionWidget( "Direction", transform.Translation );
        }
    }

    DESERT_REGISTER_CUSTOM_COMPONENT(
         ECS::TransformComponent, "Transform", false,
         ( []( ECS::Entity& e, ::Desert::Core::Scene* s, const ComponentEditContext& )
           { TransformComponentWidget().Render( e, s ); } ) )

} // namespace Desert::Editor