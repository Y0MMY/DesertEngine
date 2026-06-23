#include "SceneHierarchyPanel.hpp"
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Assets/Prefab/PrefabAsset.hpp>
#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Editor/Core/EditorResources.hpp>
#include <Editor/Core/ThemeManager.hpp>
#include <Editor/Core/ImGuiUtilities.hpp>
#include <Editor/Core/IconsMaterialDesignIcons.hpp>

#include <ImGui/imgui_internal.h>

#include <Editor/Builtin/BuiltinMeshRegistry.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    const char* SceneHierarchyPanel::GetEntityTypeName( const ECS::Entity& entity )
    {
        if ( entity.HasComponent<ECS::CameraComponent>() )
            return "CameraActor";
        if ( entity.HasComponent<ECS::DirectionLightComponent>() )
            return "DirectionalLight";
        if ( entity.HasComponent<ECS::PointLightComponent>() )
            return "PointLight";
        if ( entity.HasComponent<ECS::SkyboxComponent>() )
            return "SkyboxActor";
        if ( entity.HasComponent<ECS::SkinnedMeshComponent>() )
            return "SkinnedMeshActor";
        if ( entity.HasComponent<ECS::StaticMeshComponent>() )
            return "StaticMeshActor";
        return "Actor";
    }

    void SceneHierarchyPanel::DrawEntityNode( ECS::Entity& entity )
    {
        const std::string& name = entity.GetComponent<ECS::TagComponent>().Tag;
        const auto         UUID = entity.GetComponent<ECS::UUIDComponent>().UUID;

        if ( m_HierarchyFilter.IsActive() && !m_HierarchyFilter.PassFilter( name.c_str() ) )
            return;

        Utils::ImGuiUtilities::PushID();

        std::string uuidStr      = UUID.ToString();
        bool        hasChildren  = entity.HasComponent<ECS::RelationshipComponent>() &&
                                   !entity.GetComponent<ECS::RelationshipComponent>().Children.empty();

        const auto& selectedEntity = Core::SelectionManager::GetSelected();
        const bool  isSelected     = selectedEntity.has_value() && *selectedEntity == UUID;

        ImGuiTreeNodeFlags nodeFlags = isSelected ? ImGuiTreeNodeFlags_Selected : 0;
        nodeFlags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_FramePadding |
                     ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_SpanAvailWidth;
        if ( !hasChildren )
            nodeFlags |= ImGuiTreeNodeFlags_Leaf;

        const bool isPrefab = entity.HasComponent<ECS::PrefabComponent>();

        const char* icon = ICON_MDI_CUBE_OUTLINE;
        if ( entity.HasComponent<ECS::CameraComponent>() )
            icon = ICON_MDI_CAMERA;
        else if ( entity.HasComponent<ECS::DirectionLightComponent>() ||
                  entity.HasComponent<ECS::PointLightComponent>() )
            icon = ICON_MDI_LIGHTBULB;
        else if ( entity.HasComponent<ECS::SkyboxComponent>() )
            icon = ICON_MDI_EARTH;
        else if ( isPrefab )
            icon = ICON_MDI_PACKAGE_VARIANT;

        // Column 0: icon tree node + name
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex( 0 );

        // Prefab root entities get a distinctive teal tint
        const ImVec4 iconColor = isPrefab
            ? ImVec4( 0.3f, 0.9f, 0.8f, 1.0f )
            : ThemeManager::GetIconColor();

        ImGui::PushStyleColor( ImGuiCol_Text, iconColor );
        bool nodeOpen = ImGui::TreeNodeEx( (void*)(uint64_t)entity.GetHandle(), nodeFlags, "%s", icon );
        ImGui::PopStyleColor();

        if ( ImGui::IsItemClicked() )
            Core::SelectionManager::SetSelected( UUID );

        if ( ImGui::BeginDragDropSource() )
        {
            ImGui::SetDragDropPayload( "ENTITY_RELATIONSHIP", &UUID, sizeof( Common::UUID ) );
            ImGui::TextUnformatted( name.c_str() );
            ImGui::EndDragDropSource();
        }

        if ( ImGui::BeginDragDropTarget() )
        {
            if ( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( "ENTITY_RELATIONSHIP" ) )
            {
                Common::UUID childUUID     = *(const Common::UUID*)payload->Data;
                auto         childEntityRef = m_Scene->FindEntityByID( childUUID );
                if ( childEntityRef )
                    m_Scene->Attach( entity, const_cast<ECS::Entity&>( childEntityRef.value().get() ) );
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        // Prefab name gets the same teal tint; suffix tag is subtle
        if ( isPrefab )
        {
            ImGui::PushStyleColor( ImGuiCol_Text, iconColor );
            ImGui::TextUnformatted( name.c_str() );
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled( "[Prefab]" );
        }
        else
        {
            ImGui::TextUnformatted( name.c_str() );
        }

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

            ImGui::Separator();
            if ( ImGui::Selectable( ICON_MDI_PACKAGE_VARIANT " Instantiate Prefab..." ) )
                ImGui::OpenPopup( "InstantiatePrefabPopup" );

            ImGui::EndPopup();
        }

        DrawInstantiatePrefabPopup();

        // Column 1: type label
        ImGui::TableSetColumnIndex( 1 );
        ImGui::TextDisabled( "%s", GetEntityTypeName( entity ) );

        if ( nodeOpen )
        {
            if ( hasChildren )
            {
                auto& rel      = entity.GetComponent<ECS::RelationshipComponent>();
                auto& registry = *entity.GetRegistry();
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

    void SceneHierarchyPanel::OnUIRender()
    {
        ImRect windowRect = { ImGui::GetWindowContentRegionMin(), ImGui::GetWindowContentRegionMax() };

        auto AddEntity = []( const std::shared_ptr<Desert::Core::Scene>& scene )
        {
            if ( ImGui::BeginMenu( "Add" ) )
            {
                if ( ImGui::Selectable( "Empty Entity" ) )
                    scene->CreateNewEntity( "Empty Entity" );

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
                        scene->CreateNewEntity( "Spot Light" );
                    }
                    ImGui::EndMenu();
                }

                if ( ImGui::Selectable( "Skybox" ) )
                    scene->CreateNewEntity( "Skybox" ).AddComponent<ECS::SkyboxComponent>();

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
                    scene->CreateNewEntity( "Camera" ).AddComponent<ECS::CameraComponent>();

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
                        auto& cubeMesh      = scene->CreateNewEntity( "Cube" ).AddComponent<ECS::StaticMeshComponent>();
                        cubeMesh.MeshHandle = BuiltinMeshRegistry::Get( BuiltinMeshType::Cube );
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
            ImGui::OpenPopup( "AddEntity" );

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

        if ( ImGui::BeginPopupContextWindow() )
        {
            ImGui::Separator();
            AddEntity( m_Scene );
            ImGui::Separator();
            if ( ImGui::MenuItem( ICON_MDI_PACKAGE_VARIANT " Instantiate Prefab..." ) )
                ImGui::OpenPopup( "InstantiatePrefabPopup" );
            ImGui::EndPopup();
        }

        DrawInstantiatePrefabPopup();

        // Entity table
        {
            constexpr ImGuiTableFlags tableFlags =
                 ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;

            ImGui::PushStyleVar( ImGuiStyleVar_CellPadding, ImVec2( 4.0f, 2.0f ) );
            if ( ImGui::BeginTable( "##outliner", 2, tableFlags ) )
            {
                ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_WidthStretch );
                ImGui::TableSetupColumn( "Type", ImGuiTableColumnFlags_WidthFixed, 110.0f );
                ImGui::TableHeadersRow();

                auto& registry = m_Scene->GetRegistry();
                auto  view     = registry.view<ECS::UUIDComponent>();
                for ( auto entityHandle : view )
                {
                    ECS::Entity entity( entityHandle, registry );
                    bool hasParent = entity.HasComponent<ECS::RelationshipComponent>() &&
                                     entity.GetComponent<ECS::RelationshipComponent>().Parent != entt::null;
                    if ( !hasParent )
                        DrawEntityNode( entity );
                }

                ImGui::EndTable();
            }
            ImGui::PopStyleVar();

            if ( ImGui::BeginDragDropTargetCustom( windowRect, ImGui::GetCurrentWindow()->ID ) )
            {
                if ( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( "ENTITY_RELATIONSHIP" ) )
                {
                    Common::UUID uuid      = *(const Common::UUID*)payload->Data;
                    auto         entityRef = m_Scene->FindEntityByID( uuid );
                    if ( entityRef )
                    {
                        // Unparent to root — TODO: implement Scene::Detach
                    }
                }

                if ( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( "PREFAB_FILE" ) )
                {
                    std::string path( static_cast<const char*>( payload->Data ),
                                      static_cast<size_t>( payload->DataSize ) - 1 );
                    if ( m_AssetManager )
                    {
                        auto prefabAsset = m_AssetManager->FindByPath<Assets::PrefabAsset>( path );
                        if ( !prefabAsset )
                        {
                            prefabAsset = const_cast<Assets::AssetManager&>( *m_AssetManager )
                                .CreateAsset<Assets::PrefabAsset>( Assets::AssetPriority::High, path );
                        }
                        if ( prefabAsset )
                        {
                            if ( !prefabAsset->IsReadyForUse() )
                                prefabAsset->Load();
                            prefabAsset->Instantiate( m_Scene.get(), *m_AssetManager, nullptr );
                        }
                    }
                }

                ImGui::EndDragDropTarget();
            }
        }
    }

    void SceneHierarchyPanel::DrawInstantiatePrefabPopup()
    {
        if ( ImGui::BeginPopupModal( "InstantiatePrefabPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
        {
            ImGui::TextUnformatted( "Prefab path (.lprefab):" );
            ImGui::SetNextItemWidth( 420.0f );
            Utils::ImGuiUtilities::InputText( m_PrefabInstantiatePath, "##PrefabInstPath" );

            ImGui::Spacing();

            if ( ImGui::Button( "Instantiate", ImVec2( 130, 0 ) ) && !m_PrefabInstantiatePath.empty() )
            {
                if ( m_AssetManager )
                {
                    auto prefabAsset = m_AssetManager->FindByPath<Assets::PrefabAsset>( m_PrefabInstantiatePath );
                    if ( !prefabAsset )
                    {
                        prefabAsset = const_cast<Assets::AssetManager&>( *m_AssetManager )
                            .CreateAsset<Assets::PrefabAsset>( Assets::AssetPriority::High,
                                                               m_PrefabInstantiatePath );
                    }
                    if ( prefabAsset )
                    {
                        if ( !prefabAsset->IsReadyForUse() )
                            prefabAsset->Load();
                        prefabAsset->Instantiate( m_Scene.get(), *m_AssetManager, nullptr );
                    }
                    else
                    {
                        LOG_ERROR( "Could not load prefab: {0}", m_PrefabInstantiatePath );
                    }
                }
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();
            if ( ImGui::Button( "Cancel", ImVec2( 90, 0 ) ) )
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }
    }

} // namespace Desert::Editor
