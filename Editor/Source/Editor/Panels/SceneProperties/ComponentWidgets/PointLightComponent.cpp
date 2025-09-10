#include "PointLightComponent.hpp"

#include <Editor/Core/ImGuiUtilities.hpp>

#include <ImGui/imgui.h>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    PointLightComponentWidget::PointLightComponentWidget() : ComponentWidget( "Point Light" )
    {
    }

    void PointLightComponentWidget::Render( ECS::Entity& entity )
    {
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 2, 2 ) );
        ImGui::Columns( 2 );
        ImGui::Separator();

        auto& pointLight = entity.GetComponent<ECS::PointLightComponent>();

        Utils::ImGuiUtilities::Property( "Radius", pointLight.Radius, 0.0f, 100.0f );
        Utils::ImGuiUtilities::Property( "Colour", pointLight.Color, true,
                                         Utils::ImGuiUtilities::PropertyFlag::ColorProperty );
        Utils::ImGuiUtilities::Property( "Intensity", pointLight.Intensity, 0.0f, 4.0f );
        Utils::ImGuiUtilities::Property( "Show radius", pointLight.ShowRadius );

        ImGui::Columns( 1 );
        ImGui::Separator();
        ImGui::PopStyleVar();
    }

} // namespace Desert::Editor