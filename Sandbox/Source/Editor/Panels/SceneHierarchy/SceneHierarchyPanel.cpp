#pragma once

#include "SceneHierarchyPanel.hpp"

#include <ImGui/imgui.h>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>

#include <Editor/Core/Selection/SelectionManager.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    void SceneHierarchyPanel::AddComponent( const ECS::Entity& entity )
    {
        if ( ImGui::BeginMenu( "Add Component" ) )
        {
            if ( !entity.HasComponent<ECS::StaticMeshComponent>() && ImGui::MenuItem( "Static Mesh" ) )
            {
                auto& newEtity = entity.AddComponent<ECS::StaticMeshComponent>();
            }

            /*if ( !entity.HasComponent<ECS::MaterialComponent>() && ImGui::MenuItem( "Material" ) )
            {
                entity.AddComponent<ECS::MaterialComponent>();
            }*/

            if ( !entity.HasComponent<ECS::SkyboxComponent>() && ImGui::MenuItem( "Skybox" ) )
            {
                entity.AddComponent<ECS::SkyboxComponent>();
            }

            if ( !entity.HasComponent<ECS::DirectionLightComponent>() && ImGui::MenuItem( "Directional Light" ) )
            {
                entity.AddComponent<ECS::DirectionLightComponent>();
            }

            ImGui::EndMenu();
        }

        ImGui::PopStyleColor( 2 );
        ImGui::EndPopup();
    }

    void SceneHierarchyPanel::OnUIRender()

    {
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 8, 8 ) );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 4.0f );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 3.0f );
        ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( 0.15f, 0.15f, 0.17f, 1.0f ) );
        ImGui::PushStyleColor( ImGuiCol_Border, ImVec4( 0.08f, 0.08f, 0.10f, 1.0f ) );

        ImGui::Begin( m_PanelName.c_str(), nullptr, ImGuiWindowFlags_NoScrollbar );

        ImGui::PushStyleColor( ImGuiCol_FrameBg, ImVec4( 0.11f, 0.11f, 0.12f, 1.0f ) );
        ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, ImVec4( 0.13f, 0.13f, 0.14f, 1.0f ) );
        ImGui::PushStyleColor( ImGuiCol_FrameBgActive, ImVec4( 0.09f, 0.09f, 0.10f, 1.0f ) );
        ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.95f, 0.95f, 0.95f, 1.0f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, 1.0f );
        ImGui::PushStyleColor( ImGuiCol_Border, ImVec4( 0.2f, 0.2f, 0.25f, 1.0f ) );

        if ( ImGui::InputText( "##SceneName", &m_Scene->GetSceneName()[0],
                               ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll ) )
        {
        }

        ImGui::PopStyleVar();
        ImGui::PopStyleColor( 6 );
        ImGui::Dummy( ImVec2( 0, 8 ) );

        auto& view = m_Scene->GetAllEntities();
        for ( auto& entity : view )
        {
            std::string& tag   = entity.GetComponent<ECS::TagComponent>().Tag;
            const auto   UUID  = entity.GetComponent<ECS::UUIDComponent>().UUID;
            const auto   UUIDs = UUID.ToString();

            const auto& selectedEntity = Core::SelectionManager::GetSelected();
            const bool  isSelected     = selectedEntity.has_value() && *selectedEntity == UUID;

            if ( isSelected )
            {
                ImGui::PushStyleColor( ImGuiCol_Header, ImVec4( 0.22f, 0.42f, 0.69f, 1.0f ) );
                ImGui::PushStyleColor( ImGuiCol_HeaderHovered, ImVec4( 0.25f, 0.46f, 0.75f, 1.0f ) );
                ImGui::PushStyleColor( ImGuiCol_HeaderActive, ImVec4( 0.18f, 0.38f, 0.63f, 1.0f ) );
            }
            else
            {
                ImGui::PushStyleColor( ImGuiCol_Header, ImVec4( 0.18f, 0.18f, 0.20f, 0.0f ) );
                ImGui::PushStyleColor( ImGuiCol_HeaderHovered, ImVec4( 0.22f, 0.22f, 0.24f, 1.0f ) );
                ImGui::PushStyleColor( ImGuiCol_HeaderActive, ImVec4( 0.16f, 0.16f, 0.18f, 1.0f ) );
            }

            ImGui::PushID( UUIDs.c_str() );

            ImGui::PushStyleColor( ImGuiCol_Text, isSelected ? ImVec4( 1.0f, 1.0f, 1.0f, 1.0f )
                                                             : ImVec4( 0.9f, 0.9f, 0.9f, 0.9f ) );

            bool nodeOpen = ImGui::TreeNodeEx( tag.c_str(),
                                               ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                                                    ( isSelected ? ImGuiTreeNodeFlags_Selected : 0 ) );

            ImGui::PopStyleColor();

            if ( ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() )
            {
                Core::SelectionManager::SetSelected( UUID );
            }

            if ( ImGui::BeginPopupContextItem() )
            {
                ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.9f, 0.9f, 0.9f, 1.0f ) );
                ImGui::PushStyleColor( ImGuiCol_PopupBg, ImVec4( 0.13f, 0.13f, 0.15f, 0.98f ) );

                if ( ImGui::MenuItem( "Delete" ) )
                {
                    if ( isSelected )
                    {
                        Core::SelectionManager::ClearSelection();
                    }
                }

                ImGui::Separator();

                AddComponent(entity                           );
            }

            if ( nodeOpen )
            {
                ImGui::TreePop();
            }

            ImGui::PopID();
            ImGui::PopStyleColor( 3 );
        }

        if ( ImGui::BeginPopupContextWindow( "HierarchyContext",
                                             ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems ) )
        {

            ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.9f, 0.9f, 0.9f, 1.0f ) );
            ImGui::TextUnformatted( "Create Entity" );
            ImGui::PopStyleColor();
            ImGui::Separator();

            ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.8f, 0.8f, 0.85f, 1.0f ) );

            if ( ImGui::MenuItem( "Empty Entity" ) )
            {
                m_Scene->CreateNewEntity( "New Entity" );
            }

            if ( ImGui::MenuItem( "Mesh" ) )
            {
                m_Scene->CreateNewEntity( "Mesh" ).AddComponent<ECS::StaticMeshComponent>();
            }

            if ( ImGui::BeginMenu( "3D Objects" ) )
            {
                if ( ImGui::MenuItem( "Cube" ) )
                {
                }
                if ( ImGui::MenuItem( "Sphere" ) )
                {
                }
                if ( ImGui::MenuItem( "Plane" ) )
                {
                }
                ImGui::EndMenu();
            }

            if ( ImGui::BeginMenu( "Light" ) )
            {
                if ( ImGui::MenuItem( "Directional" ) )
                {
                    m_Scene->CreateNewEntity( "Directional Light" ).AddComponent<ECS::DirectionLightComponent>();
                }
                if ( ImGui::MenuItem( "Point" ) )
                {
                }
                if ( ImGui::MenuItem( "Spot" ) )
                {
                }
                ImGui::EndMenu();
            }

            if ( ImGui::BeginMenu( "Environment" ) )
            {
                if ( ImGui::MenuItem( "Skybox" ) )
                {
                    m_Scene->CreateNewEntity( "Skybox" ).AddComponent<ECS::SkyboxComponent>();
                }
                if ( ImGui::MenuItem( "Fog" ) )
                {
                    // m_Scene->CreateNewEntity( "Fog" ).AddComponent<ECS::FogComponent>();
                }
                ImGui::EndMenu();
            }

            ImGui::PopStyleColor();
            ImGui::EndPopup();
        }

        ImGui::End();
        ImGui::PopStyleColor( 1 );
        ImGui::PopStyleVar( 3 );
    }
} // namespace Desert::Editor