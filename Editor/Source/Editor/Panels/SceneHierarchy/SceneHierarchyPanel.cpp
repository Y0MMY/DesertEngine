#include "SceneHierarchyPanel.hpp"
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Editor/Core/EditorResources.hpp>
#include <Editor/Core/ThemeManager.hpp>
#include <Editor/Core/ImGuiUtilities.hpp>

#include <ImGui/imgui_internal.h>

#include <Editor/Builtin/BuiltinMeshRegistry.hpp>

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

        if ( visible && m_HierarchyFilter.IsActive() )
        {
            if ( !m_HierarchyFilter.PassFilter( (const char*)name.c_str() ) )
            {
                show = false;
            }
        }

        if ( show )
        {
            Utils::ImGuiUtilities::PushID();
            
            const auto UUID = entity.GetComponent<ECS::UUIDComponent>().UUID;
            std::string uuidStr = UUID.ToString();

            bool hasRelationship = entity.HasComponent<ECS::RelationshipComponent>();
            bool hasChildren = hasRelationship && !entity.GetComponent<ECS::RelationshipComponent>().Children.empty();

            const auto& selectedEntity = Core::SelectionManager::GetSelected();
            const bool  isSelected     = selectedEntity.has_value() && *selectedEntity == UUID;

            ImGuiTreeNodeFlags nodeFlags = ( isSelected ) ? ImGuiTreeNodeFlags_Selected : 0;

            nodeFlags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_FramePadding |
                         ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_SpanAvailWidth;

            if ( !hasChildren )
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
                icon = (char*)ICON_MDI_CAMERA;
            else if ( entity.HasComponent<ECS::DirectionLightComponent>() || entity.HasComponent<ECS::PointLightComponent>() )
                icon = (char*)ICON_MDI_LIGHTBULB;
            else if ( entity.HasComponent<ECS::SkyboxComponent>() )
                icon = (char*)ICON_MDI_EARTH;

            ImGui::PushStyleColor( ImGuiCol_Text, ThemeManager::GetIconColor() );

            bool nodeOpen = ImGui::TreeNodeEx( (void*)(uint64_t)entity.GetHandle(), nodeFlags, "%s", icon );
            
            // Selection logic
            if ( ImGui::IsItemClicked() )
            {
                Core::SelectionManager::SetSelected( UUID );
            }

            // Drag and Drop Source
            if ( ImGui::BeginDragDropSource() )
            {
                ImGui::SetDragDropPayload( "ENTITY_RELATIONSHIP", &UUID, sizeof( Common::UUID ) );
                ImGui::TextUnformatted( name.c_str() );
                ImGui::EndDragDropSource();
            }

            // Drag and Drop Target
            if ( ImGui::BeginDragDropTarget() )
            {
                if ( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( "ENTITY_RELATIONSHIP" ) )
                {
                    Common::UUID childUUID = *(const Common::UUID*)payload->Data;
                    auto childEntityRef = m_Scene->FindEntityByID( childUUID );
                    if ( childEntityRef )
                    {
                        m_Scene->Attach( entity, const_cast<ECS::Entity&>( childEntityRef.value().get() ) );
                    }
                }
                ImGui::EndDragDropTarget();
            }

            bool hovered = ImGui::IsItemHovered( ImGuiHoveredFlags_None );

            ImGui::PopStyleColor();
            ImGui::SameLine();

            ImGui::TextUnformatted( name.c_str() );

            if ( !active )
                ImGui::PopStyleColor();

            bool deleteEntity = false;

            if ( ImGui::BeginPopupContextItem( uuidStr.c_str() ) )
            {
                if ( ImGui::Selectable( "Delete" ) )
                    deleteEntity = true;

                ImGui::Separator();
                if ( ImGui::Selectable( "Add Child" ) )
                {
                    auto child = m_Scene->CreateNewEntity( "Child Entity" );
                    m_Scene->Attach( entity, child );
                }

                ImGui::EndPopup();
            }

            bool showButton = true;
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

            if ( nodeOpen )
            {
                if ( hasChildren )
                {
                    auto& rel = entity.GetComponent<ECS::RelationshipComponent>();
                    auto& registry = *entity.GetRegistry();
                    
                    // Recursive call for children
                    for ( auto childHandle : rel.Children )
                    {
                        ECS::Entity child( childHandle, registry );
                        DrawEntityNode( child );
                    }
                }
                ImGui::TreePop();
            }

            if ( deleteEntity )
            {
                m_Scene->DestroyEntity( entity );
                if ( isSelected )
                    Core::SelectionManager::ClearSelection();
            }

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

                if ( ImGui::BeginMenu( "Light" ) )
                {
                    if ( ImGui::Selectable( "Directional Light" ) )
                    {
                        auto entity = scene->CreateNewEntity( "Directional Light" );
                        entity.AddComponent<ECS::DirectionLightComponent>();
                    }

                    if ( ImGui::Selectable( "Point Light" ) )
                    {
                        auto entity = scene->CreateNewEntity( "Point Light" );
                        entity.AddComponent<ECS::PointLightComponent>();
                    }

                    if ( ImGui::Selectable( "Spot Light" ) )
                    {
                        auto entity = scene->CreateNewEntity( "Spot Light" );
                        //   entity.AddComponent<ECS::SpotLightComponent>();
                    }

                    ImGui::EndMenu();
                }

                if ( ImGui::Selectable( "Skybox" ) )
                {
                    scene->CreateNewEntity( "Skybox" ).AddComponent<ECS::SkyboxComponent>();
                }

                if ( ImGui::Selectable( "3D Model" ) )
                {
                    scene->CreateNewEntity( "3D Model" ).AddComponent<ECS::StaticMeshComponent>().MeshHandle =
                         Assets::AssetHandle{ 0 };
                }

                if ( ImGui::Selectable( "Skinned Model" ) )
                {
                    auto entity = scene->CreateNewEntity( "Skinned Model" );
                    entity.AddComponent<ECS::SkinnedMeshComponent>();
                    entity.AddComponent<ECS::AnimationComponent>();
                }

                if ( ImGui::Selectable( "Rigid Body" ) )
                {
                }

                if ( ImGui::Selectable( "Camera" ) )
                {
                    scene->CreateNewEntity( "Camera" ).AddComponent<ECS::CameraComponent>();
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
                        auto& cubeMesh = scene->CreateNewEntity( "Cube" ).AddComponent<ECS::StaticMeshComponent>();
                        cubeMesh.MeshHandle = BuiltinMeshRegistry::Get( BuiltinMeshType::Cube );
                    }

                    if ( ImGui::MenuItem( "Sphere" ) )
                    {
                        /*  auto& cubeMesh =
                               scene->CreateNewEntity( "Sphere" ).AddComponent<ECS::StaticMeshComponent>();
                          cubeMesh.PrimitiveShape = PrimitiveType::Sphere;*/
                    }

                    if ( ImGui::MenuItem( "Pyramid" ) )
                    {
                        /* auto& cubeMesh =
                              scene->CreateNewEntity( "Pyramid" ).AddComponent<ECS::StaticMeshComponent>();
                         cubeMesh.PrimitiveShape = PrimitiveType::Pyramid;*/
                    }

                    if ( ImGui::MenuItem( "Plane" ) )
                    {
                        /*  auto& cubeMesh =
                               scene->CreateNewEntity( "Plane" ).AddComponent<ECS::StaticMeshComponent>();
                          cubeMesh.PrimitiveShape = PrimitiveType::Plane;*/
                    }

                    if ( ImGui::MenuItem( "Cylinder" ) )
                    {
                        /*   auto& cubeMesh =
                                scene->CreateNewEntity( "Cylinder" ).AddComponent<ECS::StaticMeshComponent>();
                           cubeMesh.PrimitiveShape = PrimitiveType::Cylinder;*/
                    }

                    if ( ImGui::MenuItem( "Capsule" ) )
                    {
                        /*auto& cubeMesh =
                             scene->CreateNewEntity( "Capsule" ).AddComponent<ECS::StaticMeshComponent>();
                        cubeMesh.PrimitiveShape = PrimitiveType::Capsule;*/
                    }

                    if ( ImGui::MenuItem( "Terrain" ) )
                    {
                        /*  auto& cubeMesh =
                               scene->CreateNewEntity( "Terrain" ).AddComponent<ECS::StaticMeshComponent>();
                          cubeMesh.PrimitiveShape = PrimitiveType::Terrain;*/
                    }

                    if ( ImGui::MenuItem( "Light Cube" ) )
                    {
                        /* auto& cubeMesh =
                              scene->CreateNewEntity( "Light Cube" ).AddComponent<ECS::StaticMeshComponent>();
                         cubeMesh.PrimitiveShape = PrimitiveType::LightCube;*/
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

        bool isFilterFocused = ImGui::IsItemActive();

        ImGui::PopFont();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        if ( !m_HierarchyFilter.IsActive() && !isFilterFocused )
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

            auto& registry = m_Scene->GetRegistry();
            
            // Render root entities (those with no parent or no RelationshipComponent)
            auto view = registry.view<ECS::UUIDComponent>();
            for ( auto entityHandle : view )
            {
                ECS::Entity entity( entityHandle, registry );
                bool hasParent = false;
                if ( entity.HasComponent<ECS::RelationshipComponent>() )
                {
                    hasParent = ( entity.GetComponent<ECS::RelationshipComponent>().Parent != entt::null );
                }

                if ( !hasParent )
                {
                    DrawEntityNode( entity );
                }
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
                if ( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( "ENTITY_RELATIONSHIP" ) )
                {
                    Common::UUID uuid = *(const Common::UUID*)payload->Data;
                    auto entityRef = m_Scene->FindEntityByID( uuid );
                    if ( entityRef )
                    {
                        // Logic to unparent (or re-parent to root)
                        // TODO: Implement Scene::Detach if needed
                    }
                }
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