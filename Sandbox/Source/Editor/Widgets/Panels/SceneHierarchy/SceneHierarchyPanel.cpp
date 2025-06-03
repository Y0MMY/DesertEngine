#pragma once

#include "SceneHierarchyPanel.hpp"

#include <ImGui/imgui.h>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;
    void SceneHierarchyPanel::OnUIRender() const
    {
        ImGui::Begin( m_PanelName.c_str() );

        auto view = m_Scene->GetAllEntities();
        for ( auto entity : view )
        {
            const std::string& tag = entity.GetComponent<ECS::TagComponent>().Tag;

            const bool isSelected = true; //( m_SelectionContext == entity );
            if ( isSelected )
                ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 1, 1, 0, 1 ) );

            if ( ImGui::Selectable( tag.c_str(), isSelected ) )
            {
                // m_SelectionContext = entity;
            }

            if ( isSelected )
                ImGui::PopStyleColor();
        }

        if ( ImGui::BeginPopupContextWindow( "HierarchyContext" ) )
        {
            if ( ImGui::BeginMenu( "Create" ) )
            {
                if ( ImGui::MenuItem( "Empty Entity" ) )
                    m_Scene->CreateNewEntity( "Empty Entity" );

                if ( ImGui::MenuItem( "Cube" ) )
                {
                }

                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        /*if ( m_RenamingEntity )
        {
            ImGui::OpenPopup( "RenamePopup" );
            if ( ImGui::BeginPopupModal( "RenamePopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
            {
                ImGui::InputText( "##Rename", &m_RenameBuffer );
                if ( ImGui::Button( "OK" ) || ImGui::IsKeyPressed( ImGuiKey_Enter ) )
                {
                    m_RenamingEntity.GetComponent<TagComponent>().Tag = m_RenameBuffer;
                    m_RenamingEntity                                  = {};
                }
                ImGui::SameLine();
                if ( ImGui::Button( "Cancel" ) || ImGui::IsKeyPressed( ImGuiKey_Escape ) )
                {
                    m_RenamingEntity = {};
                }
                ImGui::EndPopup();
            }
        }*/

        ImGui::End();
    }

} // namespace Desert::Editor