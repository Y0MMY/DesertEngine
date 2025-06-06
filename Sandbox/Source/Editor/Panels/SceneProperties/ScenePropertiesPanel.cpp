#include "ScenePropertiesPanel.hpp"

#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Editor/Widgets/Controls/Controls.hpp>

#include <ImGui/imgui.h>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    void ScenePropertiesPanel::OnUIRender()
    {
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 10, 8 ) );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 4.0f );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 3.0f );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 8, 6 ) );
        ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( 0.15f, 0.15f, 0.17f, 1.0f ) );
        ImGui::PushStyleColor( ImGuiCol_Border, ImVec4( 0.08f, 0.08f, 0.10f, 1.0f ) );

        ImGui::Begin( m_PanelName.c_str(), nullptr, ImGuiWindowFlags_NoScrollbar );

        ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.9f, 0.9f, 0.9f, 1.0f ) );
        ImGui::TextUnformatted( "Entity Properties" );
        ImGui::PopStyleColor();

        ImGui::Separator();
        ImGui::Dummy( ImVec2( 0, 6 ) );

        auto selectedOpt = Core::SelectionManager::GetSelected();

        if ( selectedOpt )
        {
            const auto& selectedEntityOpt = m_Scene->FindEntityByID( selectedOpt.value() );
            if ( selectedEntityOpt )
            {
                const auto& selectedEntity = selectedEntityOpt.value().get();

                ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.7f, 0.7f, 0.8f, 0.9f ) );
                if ( selectedEntity.HasComponent<ECS::UUIDComponent>() )
                    ImGui::Text( "ID: %s",
                                 selectedEntity.GetComponent<ECS::UUIDComponent>().UUID.ToString().c_str() );
                ImGui::PopStyleColor();

                ImGui::Dummy( ImVec2( 0, 8 ) );

                if ( selectedEntity.HasComponent<ECS::TagComponent>() )
                {
                    auto tag = &selectedEntity.GetComponent<ECS::TagComponent>().Tag[0];

                    ImGui::PushStyleColor( ImGuiCol_FrameBg, ImVec4( 0.11f, 0.11f, 0.12f, 1.0f ) );
                    ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, ImVec4( 0.13f, 0.13f, 0.14f, 1.0f ) );
                    ImGui::PushStyleColor( ImGuiCol_FrameBgActive, ImVec4( 0.09f, 0.09f, 0.10f, 1.0f ) );
                    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.95f, 0.95f, 0.95f, 1.0f ) );
                    ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, 1.0f );
                    ImGui::PushStyleColor( ImGuiCol_Border, ImVec4( 0.2f, 0.2f, 0.25f, 1.0f ) );

                    ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x );
                    if ( ImGui::InputText( "##EntityName", tag,
                                           ImGuiInputTextFlags_EnterReturnsTrue |
                                                ImGuiInputTextFlags_AutoSelectAll ) )
                    {
                    }

                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor( 5 );

                    ImGui::Dummy( ImVec2( 0, 10 ) );
                }

                if ( selectedEntity.HasComponent<ECS::TransformComponent>() &&
                     !selectedEntity.HasComponent<ECS::DirectionLightComponent>() )
                {
                    if ( ImGui::CollapsingHeader( "Transform", ImGuiTreeNodeFlags_DefaultOpen ) )
                    {
                        ImGui::Dummy( ImVec2( 0, 4 ) );
                        auto& transform = selectedEntity.GetComponent<ECS::TransformComponent>();

                        Widgets::DrawVec3Control( "Position", transform.Position );
                        ImGui::Dummy( ImVec2( 0, 6 ) );
                        Widgets::DrawVec3Control( "Rotation", transform.Rotation );
                        ImGui::Dummy( ImVec2( 0, 6 ) );
                        Widgets::DrawVec3Control( "Scale", transform.Scale, 1.0f );
                    }
                }

                else
                {
                    if ( ImGui::CollapsingHeader( "Direction", ImGuiTreeNodeFlags_DefaultOpen ) )
                    {
                        ImGui::Dummy( ImVec2( 0, 4 ) );
                        auto& transform = selectedEntity.GetComponent<ECS::TransformComponent>();

                        Widgets::DrawVec3Control( "Direction", transform.Position );
                    }
                }
            }
        }
        else
        {
            ImGui::Dummy( ImVec2( 0, 20 ) );
            ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.6f, 0.6f, 0.65f, 0.8f ) );
            ImGui::SetCursorPosX( ( ImGui::GetWindowWidth() - ImGui::CalcTextSize( "No entity selected" ).x ) *
                                  0.5f );
            ImGui::Text( "No entity selected" );
            ImGui::PopStyleColor();
        }

        ImGui::End();
        ImGui::PopStyleColor( 2 );
        ImGui::PopStyleVar( 4 );
    }

} // namespace Desert::Editor