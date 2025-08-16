#include "SceneHierarchyPanel.hpp"
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Editor/Core/EditorResources.hpp>
#include <Editor/Core/ThemeManager.hpp>
#include <Editor/Core/ImGuiUtilities.hpp>

#include <ImGui/imgui_internal.h>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    void SceneHierarchyPanel::DrawEntityNode( ECS::Entity& entity )
    {
        bool show = true;

        bool visible = ImGui::IsRectVisible(
             ImVec2( ImGui::GetContentRegionMax().x, ImGui::GetTextLineHeightWithSpacing() ) );
        if ( !visible )
        {
            ImGui::NewLine();
            return;
        }

        const std::string& name  = entity.GetComponent<ECS::TagComponent>().Tag;
        const auto         UUID  = entity.GetComponent<ECS::UUIDComponent>().UUID;
        const auto         UUIDs = UUID.ToString();

        if ( visible && m_HierarchyFilter.IsActive() )
        {
            if ( !m_HierarchyFilter.PassFilter( (const char*)name.c_str() ) )
            {
                show = false;
            }
        }

        if ( show )
        {
            // ImGui::PushID((int)node);
            Utils::ImGuiUtilities::PushID();
            bool noChildren = true;

            const auto& selectedEntity = Core::SelectionManager::GetSelected();
            const bool  isSelected     = selectedEntity.has_value() && *selectedEntity == UUID;

            ImGuiTreeNodeFlags nodeFlags = ( isSelected ) ? ImGuiTreeNodeFlags_Selected : 0;

            nodeFlags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_FramePadding |
                         ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_SpanAvailWidth;

            if ( noChildren )
            {
                nodeFlags |= ImGuiTreeNodeFlags_Leaf;
            }

            bool active = true; // TODO

            if ( !active )
            {
                ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) );
            }

            char* icon = (char*)ICON_MDI_CUBE_OUTLINE;

            if ( entity.HasComponent<ECS::CameraComponent>() )
            {
                icon = (char*)ICON_MDI_CAMERA;
            }

            else if ( entity.HasComponent<ECS::DirectionLightComponent>() )
            {
                icon = (char*)ICON_MDI_LIGHTBULB;
            }

            ImGui::PushStyleColor( ImGuiCol_Text, ThemeManager::GetIconColor() );

            bool nodeOpen = ImGui::TreeNodeEx( name.c_str(), nodeFlags, "%s", icon );
            {

                // TODO: Allow clicking of icon and text. Need twice as they are separated
                if ( ImGui::IsMouseReleased( ImGuiMouseButton_Left ) && ImGui::IsItemHovered() &&
                     !ImGui::IsItemToggledOpen() )
                {
                }
            }

            if ( visible && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) &&
                 ImGui::IsItemHovered( ImGuiHoveredFlags_None ) )
            {
            }

            bool hovered = ImGui::IsItemHovered( ImGuiHoveredFlags_None );

            ImGui::PopStyleColor();
            ImGui::SameLine();

            ImGui::TextUnformatted( (const char*)name.c_str() );

            if ( !active )
                ImGui::PopStyleColor();

            bool deleteEntity = false;

            if ( ImGui::BeginPopupContextItem( (const char*)name.c_str() ) )
            {
                if ( ImGui::Selectable( "Copy" ) )
                {
                }

                if ( ImGui::Selectable( "Cut" ) )
                {
                }

                ImGui::Separator();

                if ( ImGui::Selectable( "Duplicate" ) )
                {
                }
                if ( ImGui::Selectable( "Delete" ) )
                {
                    deleteEntity = true;
                }

                ImGui::Separator();
                if ( ImGui::Selectable( "Rename" ) )
                {
                }
                ImGui::Separator();

                if ( ImGui::Selectable( "Add Child" ) )
                {
                }

                if ( ImGui::Selectable( "Zoom to" ) )
                {
                }
                ImGui::EndPopup();
            }

            if ( ImGui::IsItemClicked() && !deleteEntity )
            {
                Core::SelectionManager::SetSelected( UUID );
            }

            if ( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) &&
                 ImGui::IsItemHovered( ImGuiHoveredFlags_None ) )
            {
            }

#if 1
            bool showButton = true; // hovered || !active;

            if ( showButton )
            {
                ImGui::SameLine( ImGui::GetWindowContentRegionMax().x -
                                 ImGui::CalcTextSize( ICON_MDI_EYE ).x * 2.0f );
                ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.7f, 0.7f, 0.7f, 0.0f ) );
                if ( ImGui::Button( active ? ICON_MDI_EYE : ICON_MDI_EYE_OFF ) )
                {
                }
                ImGui::PopStyleColor();
            }
#endif

            if ( nodeOpen == false )
            {
                Utils::ImGuiUtilities::PopID();
                return;
            }

            const ImColor TreeLineColor = ImColor( 128, 128, 128, 128 );
            const float   SmallOffsetX  = 6.0f;
            ImDrawList*   drawList      = ImGui::GetWindowDrawList();

            ImVec2 verticalLineStart = ImGui::GetCursorScreenPos();
            verticalLineStart.x += SmallOffsetX; // to nicely line up with the arrow symbol
            ImVec2 verticalLineEnd = verticalLineStart;

            drawList->AddLine( verticalLineStart, verticalLineEnd, TreeLineColor );

            ImGui::TreePop();
            Utils::ImGuiUtilities::PopID();
        }
    }

    void SceneHierarchyPanel::OnUIRender()
    {
        auto flags = ImGuiWindowFlags_NoCollapse;

        ImRect windowRect = { ImGui::GetWindowContentRegionMin(), ImGui::GetWindowContentRegionMax() };

        auto AddEntity = []( const std::shared_ptr<Desert::Core::Scene>& scene )
        {
            if ( ImGui::BeginMenu( "Add" ) )
            {
                if ( ImGui::Selectable( "Empty Entity" ) )
                {
                    scene->CreateNewEntity( "Empty Entity" );
                }

                if ( ImGui::Selectable( "Light" ) )
                {
                    scene->CreateNewEntity( "Directional Light" ).AddComponent<ECS::DirectionLightComponent>();
                }

                if ( ImGui::Selectable( "3D Model" ) )
                {
                    scene->CreateNewEntity( "3D Model" ).AddComponent<ECS::StaticMeshComponent>();
                }

                if ( ImGui::Selectable( "Rigid Body" ) )
                {
                }

                if ( ImGui::Selectable( "Camera" ) )
                {
                    scene->CreateNewEntity( "Directional Light" ).AddComponent<ECS::CameraComponent>();
                }

                if ( ImGui::Selectable( "Sprite" ) )
                {
                }

                if ( ImGui::Selectable( "Lua Script" ) )
                {
                }

                if ( ImGui::BeginMenu( "Primitive" ) )
                {

                    if ( ImGui::MenuItem( "Cube" ) )
                    {
                    }

                    if ( ImGui::MenuItem( "Sphere" ) )
                    {
                    }

                    if ( ImGui::MenuItem( "Pyramid" ) )
                    {
                    }

                    if ( ImGui::MenuItem( "Plane" ) )
                    {
                    }

                    if ( ImGui::MenuItem( "Cylinder" ) )
                    {
                    }

                    if ( ImGui::MenuItem( "Capsule" ) )
                    {
                    }

                    if ( ImGui::MenuItem( "Terrain" ) )
                    {
                    }

                    if ( ImGui::MenuItem( "Light Cube" ) )
                    {
                    }

                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
        };

        ImGui::PushStyleColor( ImGuiCol_MenuBarBg, ImGui::GetStyleColorVec4( ImGuiCol_TabActive ) );

        if ( ImGui::Button( ICON_MDI_PLUS ) )
        {
            // Add Entity Menu
            ImGui::OpenPopup( "AddEntity" );
        }

        if ( ImGui::BeginPopup( "AddEntity" ) )
        {
            AddEntity( m_Scene );
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        ImGui::TextUnformatted( ICON_MDI_MAGNIFY );
        ImGui::SameLine();

        ImGui::PushFont( EditorResources::GetBoldFont() );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, 0.0f );
        ImGui::PushStyleColor( ImGuiCol_FrameBg, IM_COL32( 0, 0, 0, 0 ) );
        m_HierarchyFilter.Draw( "##HierarchyFilter",
                                ImGui::GetContentRegionAvail().x - ImGui::GetStyle().IndentSpacing );
        Utils::ImGuiUtilities::DrawItemActivityOutline( 2.0f, false, ImColor( 80, 80, 80 ) );
        ImGui::PopFont();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        if ( !m_HierarchyFilter.IsActive() )
        {
            ImGui::SameLine();
            ImGui::PushFont( EditorResources::GetBoldFont() );
            ImGui::SetCursorPosX( ImGui::GetFontSize() * 4.0f );
            ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 0.0f, ImGui::GetStyle().FramePadding.y ) );
            ImGui::TextUnformatted( "Search..." );
            ImGui::PopFont();
            ImGui::PopStyleVar();
        }

        ImGui::PopStyleColor();
        ImGui::Unindent();

        // Right click popup
        if ( ImGui::BeginPopupContextWindow() )
        {
            /*if ( !m_Editor->GetCopiedEntity().empty() &&
                 registry.valid( m_Editor->GetCopiedEntity().front() ) )
            {
                if ( ImGui::Selectable( "Paste" ) )
                {
                    for ( auto entity : m_Editor->GetCopiedEntity() )
                    {
                        auto   scene        = Application::Get().GetSceneManager()->GetCurrentScene();
                        Entity copiedEntity = { entity, scene };
                        if ( !copiedEntity.Valid() )
                        {
                            m_Editor->SetCopiedEntity( {} );
                        }
                        else
                        {
                            scene->DuplicateEntity( copiedEntity );

                            if ( m_Editor->GetCutCopyEntity() )
                            {
                                copiedEntity.GetScene()->DestroyEntity( copiedEntity );
                            }
                        }
                    }
                }
            }
            else
            {
                ImGui::TextDisabled( "Paste" );
            }*/

            ImGui::Separator();

            AddEntity( m_Scene );

            ImGui::EndPopup();
        }
        {

            ImGui::Indent();

            auto& view = m_Scene->GetAllEntities();
            for ( auto& entity : view )
            {
                DrawEntityNode( const_cast<ECS::Entity&>( entity ) );
            }

            // Only supports one scene
            ImVec2 min_space = ImGui::GetWindowContentRegionMin();
            ImVec2 max_space = ImGui::GetWindowContentRegionMax();

            float yOffset = std::max( 45.0f, ImGui::GetScrollY() ); // Dont include search bar
            min_space.x += ImGui::GetWindowPos().x + 1.0f;
            min_space.y += ImGui::GetWindowPos().y + 1.0f + yOffset;
            max_space.x += ImGui::GetWindowPos().x - 1.0f;
            max_space.y += ImGui::GetWindowPos().y - 1.0f + ImGui::GetScrollY();
            ImRect bb{ min_space, max_space };

            if ( ImGui::BeginDragDropTargetCustom( windowRect, ImGui::GetCurrentWindow()->ID ) )
            {

                ImGui::EndDragDropTarget();
            }
        }
        // ImGui::EndChild();
        // ImGui::PopStyleColor();

        if ( ImGui::IsWindowFocused() )
        {
        }
    }

} // namespace Desert::Editor