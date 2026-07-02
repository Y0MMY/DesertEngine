#include "SceneHierarchyPanel.hpp"
#include <Editor/Core/DragPayloads.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Assets/Prefab/PrefabAsset.hpp>
#include <Engine/Geometry/ProceduralCharacterFactory.hpp>
#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Editor/Core/EditorResources.hpp>
#include <Editor/Core/ThemeManager.hpp>
#include <Editor/Core/ImGuiUtilities.hpp>
#include <Editor/Core/IconsMaterialDesignIcons.hpp>

#include <ImGui/imgui_internal.h>

#include <Editor/Builtin/BuiltinMeshRegistry.hpp>
#include <Common/Core/Constants.hpp>

#include <filesystem>

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
        if ( entity.HasComponent<ECS::SpotLightComponent>() )
            return "SpotLight";
        if ( entity.HasComponent<ECS::SkyboxComponent>() )
            return "SkyboxActor";
        if ( entity.HasComponent<ECS::TerrainComponent>() )
            return "TerrainActor";
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

        // Hidden entities (VisibilityComponent.Visible == false) render dimmed with an "eye-off" toggle.
        const bool visible = !entity.HasComponent<ECS::VisibilityComponent>() ||
                             entity.GetComponent<ECS::VisibilityComponent>().Visible;

        const char* icon = ICON_MDI_CUBE_OUTLINE;
        if ( entity.HasComponent<ECS::CameraComponent>() )
            icon = ICON_MDI_CAMERA;
        else if ( entity.HasComponent<ECS::SpotLightComponent>() )
            icon = ICON_MDI_SPOTLIGHT;
        else if ( entity.HasComponent<ECS::DirectionLightComponent>() ||
                  entity.HasComponent<ECS::PointLightComponent>() )
            icon = ICON_MDI_LIGHTBULB;
        else if ( entity.HasComponent<ECS::SkyboxComponent>() )
            icon = ICON_MDI_EARTH;
        else if ( entity.HasComponent<ECS::TerrainComponent>() )
            icon = ICON_MDI_TERRAIN;
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
            ImGui::SetDragDropPayload( ::Desert::Editor::DragPayloads::EntityRelationship, &UUID, sizeof( Common::UUID ) );
            ImGui::TextUnformatted( name.c_str() );
            ImGui::EndDragDropSource();
        }

        if ( ImGui::BeginDragDropTarget() )
        {
            if ( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( ::Desert::Editor::DragPayloads::EntityRelationship ) )
            {
                Common::UUID childUUID     = *(const Common::UUID*)payload->Data;
                auto         childEntityRef = m_Scene->FindEntityByID( childUUID );
                if ( childEntityRef )
                    m_Scene->Attach( entity, const_cast<ECS::Entity&>( childEntityRef.value().get() ) );
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        // Name colour: prefab teal, dimmed when hidden. Prefab tag stays subtle.
        ImVec4 nameColor = isPrefab ? iconColor : ImGui::GetStyleColorVec4( ImGuiCol_Text );
        if ( !visible )
            nameColor = ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled );
        ImGui::PushStyleColor( ImGuiCol_Text, nameColor );
        ImGui::TextUnformatted( name.c_str() );
        ImGui::PopStyleColor();
        if ( isPrefab )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "[Prefab]" );
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
                m_OpenInstantiatePrefab = true; // deferred: OpenPopup at panel scope (see OnUIRender)

            ImGui::EndPopup();
        }

        // Column 1: inline visibility eye (UE5-style) + type label.
        ImGui::TableSetColumnIndex( 1 );
        ImGui::PushStyleColor( ImGuiCol_Text, visible ? ThemeManager::GetIconColor()
                                                      : ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) );
        ImGui::TextUnformatted( visible ? ICON_MDI_EYE_OUTLINE : ICON_MDI_EYE_OFF_OUTLINE );
        ImGui::PopStyleColor();
        if ( ImGui::IsItemClicked() )
            m_Scene->SetVisibleRecursive( entity, !visible ); // toggles the entity + its subtree
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( visible ? "Hide" : "Show" );
        ImGui::SameLine();
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

        // Deferred: destroying here would invalidate the entt view being iterated in OnUIRender
        // (deleting a parent destroys its whole subtree at once). Process after EndTable.
        if ( deleteEntity )
            m_PendingDelete = UUID;

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
                        auto entity = scene->CreateNewEntity( "Spot Light" );
                        entity.AddComponent<ECS::SpotLightComponent>();
                    }
                    ImGui::EndMenu();
                }

                if ( ImGui::Selectable( "Skybox" ) )
                    scene->CreateNewEntity( "Skybox" ).AddComponent<ECS::SkyboxComponent>();

                if ( ImGui::Selectable( "Terrain" ) )
                    scene->CreateNewEntity( "Terrain" ).AddComponent<ECS::TerrainComponent>();

                // NOTE: no standalone "Material" entity — a MaterialComponent is meaningless without
                // geometry. It is added ONTO a renderable entity via Details -> Add Component.

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

                // Code-generated rounded humanoid mannequin (no import needed). Renders in its bind/A-pose;
                // pick Idle/Walk/Run/Jump in Details ▸ Animation, or parent it to a Character Controller so
                // LocomotionSystem drives it from movement.
                if ( ImGui::Selectable( "Character (Procedural)" ) )
                {
                    auto entity = scene->CreateNewEntity( "Character" );
                    entity.AddComponent<ECS::SkinnedMeshComponent>().MeshHandle =
                         Geometry::ProceduralCharacterFactory::GetHumanoidMesh();
                    entity.AddComponent<ECS::AnimationComponent>();
                }

                if ( ImGui::Selectable( "Rigid Body" ) )
                {
                }

                if ( ImGui::Selectable( "Camera" ) )
                {
                    // Spawn at the editor viewpoint (UE "Create Camera Here") instead of the origin — a
                    // camera at (0,0,0) sits on top of / behind the editor camera, so its gizmo would be
                    // clipped at the near plane and look "missing". Placed a few units ahead of the eye so
                    // its icon + frustum are immediately visible and frame the current view.
                    auto camEntity = scene->CreateNewEntity( "Camera" );
                    camEntity.AddComponent<ECS::CameraComponent>();
                    if ( auto active = scene->GetActiveCamera() )
                    {
                        const glm::mat4 world = glm::inverse( active->GetViewMatrix() );
                        const glm::vec3 eye   = glm::vec3( world[3] );
                        const glm::vec3 fwd   = -glm::normalize( glm::vec3( world[2] ) );

                        auto& tf       = camEntity.GetComponent<ECS::TransformComponent>();
                        tf.Translation = eye + fwd * 4.0f;
                        tf.Rotation = glm::eulerAngles( glm::quatLookAt( fwd, glm::vec3( 0.0f, 1.0f, 0.0f ) ) );
                    }
                }

                if ( ImGui::Selectable( "Sprite" ) )
                {
                }

                if ( ImGui::Selectable( "Lua Script" ) )
                {
                }

                // Popular GI test scene: white floor/back + RED left wall + GREEN right wall (open top so the
                // directional sun reaches the interior), plus a white sphere + tall box in the middle. With
                // SSGI on (deferred path), the coloured walls bleed onto the white centre objects.
                if ( ImGui::Selectable( "Cornell Box (GI Test)" ) )
                {
                    auto mkBox = [&]( const char* name, glm::vec3 pos, glm::vec3 scale, glm::vec4 albedo )
                    {
                        auto  e  = scene->CreateNewEntity( name );
                        e.AddComponent<ECS::StaticMeshComponent>().Primitive = Geometry::PrimitiveType::Cube;
                        auto& tf       = e.GetComponent<ECS::TransformComponent>();
                        tf.Translation = pos;
                        tf.Scale       = scale;
                        auto& mc       = e.AddComponent<ECS::MaterialComponent>();
                        mc.ShaderName  = "StaticMeshPBR"; // stays on the batched PBR (G-buffer) path
                        mc.Params.push_back( ECS::MaterialParamOverride{ "AlbedoColor", albedo } );
                        mc.Params.push_back( ECS::MaterialParamOverride{ "RoughnessFactor", glm::vec4( 0.9f ) } );
                    };
                    const glm::vec4 white( 0.82f, 0.82f, 0.80f, 1.0f );
                    const glm::vec4 red( 0.85f, 0.10f, 0.10f, 1.0f );
                    const glm::vec4 green( 0.10f, 0.70f, 0.15f, 1.0f );
                    mkBox( "CB_Floor", { 0.0f, 0.0f, 0.0f }, { 4.0f, 0.1f, 4.0f }, white );
                    mkBox( "CB_Back", { 0.0f, 2.0f, -2.0f }, { 4.0f, 4.0f, 0.1f }, white );
                    mkBox( "CB_LeftRed", { -2.0f, 2.0f, 0.0f }, { 0.1f, 4.0f, 4.0f }, red );
                    mkBox( "CB_RightGreen", { 2.0f, 2.0f, 0.0f }, { 0.1f, 4.0f, 4.0f }, green );

                    auto mkWhite = [&]( const char* name, Geometry::PrimitiveType prim, glm::vec3 pos, glm::vec3 scale )
                    {
                        auto  e  = scene->CreateNewEntity( name );
                        e.AddComponent<ECS::StaticMeshComponent>().Primitive = prim;
                        auto& tf       = e.GetComponent<ECS::TransformComponent>();
                        tf.Translation = pos;
                        tf.Scale       = scale;
                    };
                    mkWhite( "CB_Sphere", Geometry::PrimitiveType::Sphere, { -0.8f, 0.9f, 0.4f }, glm::vec3( 0.9f ) );
                    mkWhite( "CB_TallBox", Geometry::PrimitiveType::Cube, { 0.8f, 1.2f, -0.6f }, { 1.0f, 2.4f, 1.0f } );

                    // Sun tilted toward the RED wall (a parallel light can only light a vertical face that
                    // shares its X direction) so at least one coloured wall is lit and bleeds; flip its X to
                    // test the green side. (Symmetric both-wall bleed needs point-light GI — a follow-up.)
                    // ONLY add a sun if the scene has none — the engine supports a single directional light
                    // (DirectionLightsUB is one struct, not an array); a 2nd overflows its UB and crashes.
                    if ( scene->GetRegistry().view<ECS::DirectionLightComponent>().size() == 0 )
                    {
                        auto sun = scene->CreateNewEntity( "CB_Sun" );
                        sun.AddComponent<ECS::DirectionLightComponent>();
                        sun.GetComponent<ECS::TransformComponent>().Translation =
                             glm::normalize( glm::vec3( -0.6f, -1.0f, -0.2f ) );
                    }
                }

                if ( ImGui::BeginMenu( "Primitive" ) )
                {
                    if ( ImGui::MenuItem( "Cube" ) )
                    {
                        // Use the Primitive path (RuntimeMesh generated + Invalidated by MeshECSSystem) —
                        // it renders + serializes reliably, unlike the builtin procedural-handle path.
                        auto& cubeMesh     = scene->CreateNewEntity( "Cube" ).AddComponent<ECS::StaticMeshComponent>();
                        cubeMesh.Primitive = Geometry::PrimitiveType::Cube;
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
                m_OpenInstantiatePrefab = true; // deferred: OpenPopup at panel scope below
            ImGui::EndPopup();
        }

        // Open the modal at panel scope (NOT inside a context popup — that ID mismatch was why it never
        // opened). Both context-menu triggers set the flag; we open + draw here.
        if ( m_OpenInstantiatePrefab )
        {
            ImGui::OpenPopup( "InstantiatePrefabPopup" );
            m_OpenInstantiatePrefab = false;
        }
        DrawInstantiatePrefabPopup();

        // Delete key: remove the selected entity when the outliner is focused (not while typing in a field).
        // Routed through the same deferred path (processed after the entt-view iteration below).
        if ( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) &&
             !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed( ImGuiKey_Delete, false ) )
        {
            if ( const auto& sel = Core::SelectionManager::GetSelected(); sel.has_value() )
                m_PendingDelete = *sel;
        }

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
                if ( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( ::Desert::Editor::DragPayloads::EntityRelationship ) )
                {
                    Common::UUID uuid      = *(const Common::UUID*)payload->Data;
                    auto         entityRef = m_Scene->FindEntityByID( uuid );
                    if ( entityRef )
                    {
                        // Unparent to root — TODO: implement Scene::Detach
                    }
                }

                if ( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( ::Desert::Editor::DragPayloads::PrefabFile ) )
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

        // Process deferred deletion AFTER the entt view iteration above (DestroyEntity is recursive
        // and removes the whole subtree, which would corrupt the view if done mid-iteration).
        if ( m_PendingDelete.has_value() )
        {
            auto entityRef = m_Scene->FindEntityByID( *m_PendingDelete );
            if ( entityRef )
            {
                const auto& selected = Core::SelectionManager::GetSelected();
                const bool  wasSelected = selected.has_value() && *selected == *m_PendingDelete;
                m_Scene->DestroyEntity( const_cast<ECS::Entity&>( entityRef.value().get() ) );
                if ( wasSelected )
                    Core::SelectionManager::ClearSelection();
            }
            m_PendingDelete.reset();
        }
    }

    void SceneHierarchyPanel::DrawInstantiatePrefabPopup()
    {
        if ( ImGui::BeginPopupModal( "InstantiatePrefabPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
        {
            ImGui::TextUnformatted( "Prefab path (.deprefab):" );
            ImGui::SetNextItemWidth( 420.0f );
            Utils::ImGuiUtilities::InputText( m_PrefabInstantiatePath, "##PrefabInstPath" );

            static std::string s_prefabError;
            if ( !s_prefabError.empty() )
                ImGui::TextColored( ImVec4( 1.0f, 0.4f, 0.4f, 1.0f ), "%s", s_prefabError.c_str() );

            ImGui::Spacing();

            if ( ImGui::Button( "Instantiate", ImVec2( 130, 0 ) ) && !m_PrefabInstantiatePath.empty() )
            {
                namespace fs = std::filesystem;
                std::error_code ec;
                const bool exists  = fs::exists( m_PrefabInstantiatePath, ec ) &&
                                     fs::is_regular_file( m_PrefabInstantiatePath, ec );
                const bool rightExt =
                     fs::path( m_PrefabInstantiatePath ).extension() ==
                     Common::Constants::Extensions::PREFAB_EXTENSION;

                if ( !exists )
                {
                    s_prefabError = "File not found (point to an existing .deprefab file, not a folder).";
                    LOG_ERROR( "Could not load prefab, file not found: {0}", m_PrefabInstantiatePath );
                }
                else if ( !rightExt )
                {
                    s_prefabError = "Not a .deprefab file.";
                    LOG_ERROR( "Could not load prefab, wrong extension: {0}", m_PrefabInstantiatePath );
                }
                else if ( m_AssetManager )
                {
                    s_prefabError.clear();
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
                        ImGui::CloseCurrentPopup();
                    }
                    else
                    {
                        s_prefabError = "Could not load prefab.";
                        LOG_ERROR( "Could not load prefab: {0}", m_PrefabInstantiatePath );
                    }
                }
            }

            ImGui::SameLine();
            if ( ImGui::Button( "Cancel", ImVec2( 90, 0 ) ) )
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }
    }

} // namespace Desert::Editor
