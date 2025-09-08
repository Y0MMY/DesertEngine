#include "LightGizmoRenderer.hpp"
#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Editor/Core/ImGuiUtilities.hpp>

#include "../../Core/EditorResources.hpp"

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    LightGizmoRenderer::LightGizmoRenderer( const std::shared_ptr<Desert::Core::Scene>& scene ) : m_Scene( scene )
    {
    }

    void LightGizmoRenderer::Render( float width, float height, float xpos, float ypos )
    {
        const auto camera = m_Scene->GetMainCamera();
        if ( !camera )
            return;

        RenderPointLights( camera.value(), width, height, xpos, ypos );
        // RenderDirectionalLights( camera.value(), viewportStart, viewportSize );
    }

    void LightGizmoRenderer::RenderPointLights( const std::shared_ptr<Desert::Core::Camera>& camera, float width,
                                                float height, float xpos, float ypos )
    {
        auto entities = m_Scene->GetAllEntities();

        for ( auto entity : entities )
        {
            if ( !entity.HasComponent<ECS::PointLightComponent>() )
            {
                continue;
            }

            auto& light     = entity.GetComponent<ECS::PointLightComponent>();
            auto& transform = entity.GetComponent<ECS::TransformComponent>();
            auto& uuid      = entity.GetComponent<ECS::UUIDComponent>().UUID;

            const auto frustum = camera->GetFrustum();

            if ( !frustum.IsInside( transform.Translation ) )
            {
                return;
            }

            const auto mvp = camera->GetProjectionMatrix() * camera->GetViewMatrix();

            glm::vec2 screenPos =
                 Common::Math::SpaceTransformer::WorldToScreenSpace( transform.Translation, mvp, width, height );
            const char* icon     = ICON_MDI_LIGHTBULB;
            ImVec2      iconSize = ImGui::CalcTextSize( icon );

            ImGui::SetCursorPos( ImVec2( screenPos.x - iconSize.x * 0.5f, screenPos.y - iconSize.y * 0.5f ) );

            ImVec4 lightColor = ImVec4( 1.0f, 1.0f, 1.0f, 1.0f );
            ImGui::PushStyleColor( ImGuiCol_Text, lightColor );
            ImGui::TextUnformatted( icon );
            ImGui::PopStyleColor();

            if ( ImGui::IsItemHovered() )
            {
                Utils::ImGuiUtilities::Tooltip(
                     std::format( "Point Light\nIntensity: {}\nRadius: {}\nPosition: ({}, {}, {})", light.Intensity, light.Radius,  transform.Translation.x, transform.Translation.y,
                             transform.Translation.z).c_str() );

                ImGui::BeginTooltip();
                ImGui::Text( "Point Light" );
                ImGui::Text( "Intensity: %.1f", light.Intensity );
                ImGui::Text( "Radius: %.1f", light.Radius );
                ImGui::Separator();
                ImGui::Text( "Position: (%.1f, %.1f, %.1f)", transform.Translation.x, transform.Translation.y,
                             transform.Translation.z );
                ImGui::EndTooltip();
            }
        }
    }

} // namespace Desert::Editor