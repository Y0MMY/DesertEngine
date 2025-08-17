#pragma once

#include "ScenePropertiesPanel.hpp"
#include "ComponentEditor.hpp"

#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Editor/Core/EditorResources.hpp>
#include <Editor/Core/ImGuiUtilities.hpp>
#include <Editor/Widgets/Controls/Controls.hpp>
#include <ImGui/imgui.h>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    void ScenePropertiesPanel::OnUIRender()
    {
        auto selectedOpt = Core::SelectionManager::GetSelected();
        if ( !selectedOpt.has_value() )
        {
            return;
        }

        const auto& selectedEntityOpt = m_Scene->FindEntityByID( selectedOpt.value() );
        if ( !selectedEntityOpt )
        {
            return;
        }
        const auto& selectedEntity = selectedEntityOpt.value().get();

        // active checkbox
        auto activeComponent = true; // TODO
        bool active          = true; // TODO
        if ( ImGui::Checkbox( "##ActiveCheckbox", &active ) )
        {
        }
        ImGui::SameLine();
        ImGui::TextUnformatted( ICON_MDI_CUBE_OUTLINE );
        ImGui::SameLine();

        auto& tag = selectedEntity.GetComponent<ECS::TagComponent>().Tag;

        if ( true ) // TODO: debug
        {
            ImGui::Text( "ID: %s", selectedOpt.value().ToString().c_str() );
        }

        ImGui::SameLine();
        ImGui::PushItemWidth( ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() * 4.0f );
        {
            ImGui::PushFont( EditorResources::GetBoldFont() );
            if ( Utils::ImGuiUtilities::InputText( tag, "##InspectorNameChange" ) )
            {
            }
            ImGui::PopFont();
        }
        ImGui::SameLine();

        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.7f, 0.7f, 0.7f, 0.0f ) );

        if ( ImGui::Button( ICON_MDI_FLOPPY ) )
        {
            ImGui::OpenPopup( "SavePrefab" );
        }

        Utils::ImGuiUtilities::Tooltip( "Save Entity As Prefab" );

        ImGui::SameLine();
        if ( ImGui::Button( ICON_MDI_TUNE ) )
        {
            ImGui::OpenPopup( "SetDebugMode" );
        }

        ImGui::PopStyleColor();

        if ( ImGui::BeginPopup( "SetDebugMode", 3 ) )
        {
            if ( ImGui::Button( "Revert To Prefab" ) )
            {
            }

            if ( ImGui::Selectable( "Debug Mode", m_DebugMode ) )
            {
                m_DebugMode = !m_DebugMode;
            }
            ImGui::EndPopup();
        }

        if ( ImGui::BeginPopupModal( "SavePrefab", NULL, ImGuiWindowFlags_AlwaysAutoResize ) )
        {
            ImGui::Text( "Save Current Entity as a Prefab?\n\n" );
            ImGui::Separator();

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted( "Name : " );
            ImGui::SameLine();
            Utils::ImGuiUtilities::InputText( tag, "##PrefabNameChange" );

            static std::string prefabNamePath = "//Assets/Prefabs/";
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted( "Path : " );
            ImGui::SameLine();
            Utils::ImGuiUtilities::InputText( prefabNamePath, "##PrefabPathChange" );

            if ( ImGui::Button( "OK", ImVec2( 120, 0 ) ) )
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if ( ImGui::Button( "Cancel", ImVec2( 120, 0 ) ) )
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::Separator();

        if ( m_DebugMode )
        {
            ImGui::Columns( 2 );
            Utils::ImGuiUtilities::Property( "UUID", tag, Utils::ImGuiUtilities::PropertyFlag::ReadOnly );

            ImGui::Columns( 1 );
            ImGui::Separator();
        }

        ImGui::BeginChild( "Components", ImVec2( 0.0f, 0.0f ), false, ImGuiWindowFlags_None );
        static ComponentEditor componentEditor( m_AssetManager );
        componentEditor.Render( const_cast<ECS::Entity&>( selectedEntity ) );
        ImGui::EndChild();
    }

} // namespace Desert::Editor