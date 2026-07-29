#include "SceneHierarchyPanel.hpp"
#include <Editor/Core/DragPayloads.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Assets/MaterialData.hpp>
#include <Engine/Assets/Mesh/SurfaceMaterialAsset.hpp>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Core/Serialize/GLMReflect.hpp>
#include <Engine/Core/Serialize/CustomReflect.hpp>
#include <rflcpp/rfl/json.hpp>
#include <Engine/Assets/Prefab/PrefabAsset.hpp>
#include <Engine/Geometry/ProceduralCharacterFactory.hpp>
#include <Engine/Geometry/DynamicMesh.hpp>

#include <cmath>
#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Editor/Core/Commands/SceneCommands.hpp>
#include <Editor/Core/EditorResources.hpp>
#include <Editor/Core/ThemeManager.hpp>
#include <Editor/Core/ImGuiUtilities.hpp>
#include <Editor/Core/IconsMaterialDesignIcons.hpp>

#include <ImGui/imgui_internal.h>

#include <Editor/Builtin/BuiltinMeshRegistry.hpp>
#include <Common/Core/Constants.hpp>
#include <Common/Utilities/FileSystem.hpp>

#include <filesystem>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    // Creates (or reuses, by name) a StaticMeshPBR material ASSET (.demat) carrying an albedo + roughness,
    // and returns its handle to drop into a mesh material SLOT. This is the UE-style way the demo builders
    // colour their meshes — a real, editable, reusable material asset — instead of the per-entity
    // MaterialComponent override channel. Reused by name so re-spawning a demo doesn't duplicate files.
    static Assets::AssetHandle CreatePBRMaterialAsset( const Assets::AssetManager* am, const std::string& name,
                                                       const glm::vec4& albedo, float roughness )
    {
        if ( !am )
            return {};

        const std::string           ext = Common::Constants::Extensions::MATERIAL_EXTENSION;
        const std::filesystem::path dir = Common::Constants::Path::MATERIAL_PATH;
        std::error_code             ec;
        std::filesystem::create_directories( dir, ec );
        const std::filesystem::path path = dir / ( name + ext );

        if ( std::filesystem::exists( path, ec ) )
            if ( auto existing = am->FindByPath<Assets::SurfaceMaterialAsset>( path.generic_string() ) )
                return existing->GetMetadata().Handle;

        Assets::MaterialData data;
        data.MaterialId = Common::UUID();
        data.SetParam( "AlbedoColor", albedo );
        data.SetParam( "RoughnessFactor", glm::vec4( roughness, 0.0f, 0.0f, 0.0f ) );
        Common::Utils::FileSystem::WriteContentToFile( path.generic_string(), rfl::json::write( data ) );

        auto asset = const_cast<Assets::AssetManager&>( *am )
                          .CreateAsset<Assets::SurfaceMaterialAsset>( Assets::AssetPriority::High,
                                                                      path.generic_string() );
        if ( !asset )
            return {};
        Runtime::ResourceRegistry::GetMaterialService()->Register( asset );
        return asset->GetMetadata().Handle;
    }

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

        m_VisibleOrder.push_back( UUID ); // visible draw order (Shift+click range source)

        const bool isSelected = Core::SelectionManager::IsSelected( UUID );

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
        if ( entity.HasComponent<ECS::FolderComponent>() )
            icon = ICON_MDI_FOLDER;
        else if ( entity.HasComponent<ECS::CameraComponent>() )
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
        {
            // UE-style modifiers: Ctrl toggles, Shift extends the range from the primary, plain replaces.
            ImGuiIO& io = ImGui::GetIO();
            if ( io.KeyCtrl )
                Core::SelectionManager::Toggle( UUID );
            else if ( io.KeyShift )
                SelectRangeTo( UUID );
            else
                Core::SelectionManager::SetSelected( UUID );
        }

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
                // Dragging a multi-selected entity drags the whole selection. Applied after iteration.
                Common::UUID childUUID = *(const Common::UUID*)payload->Data;
                m_PendingReparent      = { Core::SelectionManager::IsSelected( childUUID )
                                                ? Core::SelectionManager::GetSelection()
                                                : std::vector<Common::UUID>{ childUUID },
                                           UUID };
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        if ( m_RenamingEntity.has_value() && *m_RenamingEntity == UUID )
        {
            // Inline rename: commit on Enter/click-away, cancel on Escape.
            ImGui::SetNextItemWidth( -FLT_MIN );
            if ( m_RenameFocusPending )
            {
                ImGui::SetKeyboardFocusHere();
                m_RenameFocusPending = false;
            }
            Utils::ImGuiUtilities::InputText( m_RenameBuffer, "##EntityRename" );
            if ( ImGui::IsItemDeactivated() )
            {
                if ( !ImGui::IsKeyPressed( ImGuiKey_Escape, false ) )
                    Commands::Rename( UUID, m_RenameBuffer ); // undoable; no-ops on empty/unchanged
                m_RenamingEntity.reset();
            }
        }
        else
        {
            // Name colour: prefab teal, dimmed when hidden. Prefab tag stays subtle.
            ImVec4 nameColor = isPrefab ? iconColor : ImGui::GetStyleColorVec4( ImGuiCol_Text );
            if ( !visible )
                nameColor = ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled );
            ImGui::PushStyleColor( ImGuiCol_Text, nameColor );
            ImGui::TextUnformatted( name.c_str() );
            ImGui::PopStyleColor();
            if ( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
            {
                m_RenamingEntity     = UUID;
                m_RenameBuffer       = name;
                m_RenameFocusPending = true;
            }
        }
        if ( isPrefab )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "[Prefab]" );
        }

        bool deleteEntity = false;
        if ( ImGui::BeginPopupContextItem( uuidStr.c_str() ) )
        {
            if ( ImGui::Selectable( "Rename" ) )
            {
                m_RenamingEntity     = UUID;
                m_RenameBuffer       = name;
                m_RenameFocusPending = true;
            }
            // An operation on a MULTI-selected entity applies to the whole selection (UE behavior).
            auto targetSet = [&]() -> std::vector<Common::UUID>
            {
                if ( Core::SelectionManager::IsSelected( UUID ) )
                    return Core::SelectionManager::GetSelection();
                return { UUID };
            };
            if ( ImGui::Selectable( Core::SelectionManager::Count() > 1 &&
                                             Core::SelectionManager::IsSelected( UUID )
                                         ? "Duplicate (selection)"
                                         : "Duplicate" ) )
                m_PendingDuplicate = targetSet(); // deferred: cloning creates entities mid-iteration
            if ( ImGui::Selectable( Core::SelectionManager::Count() > 1 &&
                                             Core::SelectionManager::IsSelected( UUID )
                                         ? "Delete (selection)"
                                         : "Delete" ) )
                deleteEntity = true;

            ImGui::Separator();
            if ( ImGui::Selectable( "Add Child" ) )
            {
                auto child = m_Scene->CreateNewEntity( "Child Entity" );
                m_Scene->Attach( entity, child );
                Commands::NotifyCreated( { child.GetComponent<ECS::UUIDComponent>().UUID } );
            }

            ImGui::Separator();
            if ( ImGui::Selectable( ICON_MDI_PACKAGE_VARIANT " Save as Prefab..." ) )
            {
                // Default path from the entity's tag (spaces -> underscores).
                std::string stem = name;
                for ( auto& ch : stem )
                    if ( ch == ' ' )
                        ch = '_';
                m_SavePrefabTarget = UUID;
                m_SavePrefabPath   = "Resources/Assets/Prefabs/" + stem +
                                   Common::Constants::Extensions::PREFAB_EXTENSION;
                m_OpenSavePrefab = true; // deferred: OpenPopup at panel scope (see OnUIRender)
            }
            if ( ImGui::Selectable( ICON_MDI_PACKAGE_VARIANT " Instantiate Prefab..." ) )
                m_OpenInstantiatePrefab = true; // deferred: OpenPopup at panel scope (see OnUIRender)

            if ( isPrefab )
            {
                ImGui::Separator();
                if ( ImGui::Selectable( ICON_MDI_PACKAGE_UP " Apply Instance Changes to Prefab..." ) )
                {
                    m_ApplyPrefabTarget = UUID;
                    m_OpenApplyPrefab   = true; // confirmation modal (overwrites the source file)
                }
                if ( ImGui::Selectable( ICON_MDI_PACKAGE_DOWN " Revert Instance to Prefab" ) )
                    m_PendingPrefabRevert = UUID; // deferred: replaces the subtree after iteration
            }

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
        {
            m_PendingDelete = Core::SelectionManager::IsSelected( UUID )
                                   ? Core::SelectionManager::GetSelection()
                                   : std::vector<Common::UUID>{ UUID };
        }

        Utils::ImGuiUtilities::PopID();
    }

    void SceneHierarchyPanel::SelectRangeTo( const Common::UUID& target )
    {
        // Range = [primary .. target] over the order the user SEES (last frame's visible tree order).
        const auto& order   = m_VisibleOrderLast;
        const auto& primary = Core::SelectionManager::GetSelected();

        const auto find = [&]( const Common::UUID& id ) -> int
        {
            for ( size_t i = 0; i < order.size(); ++i )
                if ( order[i] == id )
                    return static_cast<int>( i );
            return -1;
        };

        const int from = primary.has_value() ? find( *primary ) : -1;
        const int to   = find( target );
        if ( from < 0 || to < 0 )
        {
            Core::SelectionManager::SetSelected( target );
            return;
        }

        std::vector<Common::UUID> range;
        const int                 lo = std::min( from, to ), hi = std::max( from, to );
        for ( int i = lo; i <= hi; ++i )
            if ( order[i] != target )
                range.push_back( order[i] );
        range.push_back( target ); // clicked entity becomes the new primary
        Core::SelectionManager::SetSelection( std::move( range ) );
    }

    void SceneHierarchyPanel::OnUIRender()
    {
        // Rotate the visible-order buffers: ranges act on what was drawn (and thus seen) last frame.
        m_VisibleOrderLast = std::move( m_VisibleOrder );
        m_VisibleOrder.clear();

        ImRect windowRect = { ImGui::GetWindowContentRegionMin(), ImGui::GetWindowContentRegionMax() };

        auto AddEntity = []( const std::shared_ptr<Desert::Core::Scene>&    scene,
                             const std::shared_ptr<Assets::AssetManager>& assetManager )
        {
            // Every menu spawn is recorded as one undo step (Ctrl+Z removes what was just added).
            auto track = []( ECS::Entity& e ) -> ECS::Entity&
            {
                Commands::NotifyCreated( { e.GetComponent<ECS::UUIDComponent>().UUID } );
                return e;
            };

            if ( ImGui::BeginMenu( "Add" ) )
            {
                if ( ImGui::Selectable( "Empty Entity" ) )
                    track( scene->CreateNewEntity( "Empty Entity" ) );

                // Folder: an empty grouping node. Drag entities onto it in the outliner to group them
                // (e.g. all Cornell Box parts under one "Cornell Box" folder). Or select entities first and
                // use "Group Selected into Folder" below to auto-parent them.
                if ( ImGui::Selectable( ICON_MDI_FOLDER " Folder" ) )
                    track( scene->CreateNewEntity( "Folder" ) ).AddComponent<ECS::FolderComponent>();

                if ( Core::SelectionManager::Count() > 0 &&
                     ImGui::Selectable( ICON_MDI_FOLDER_PLUS " Group Selected into Folder" ) )
                {
                    auto  folder = track( scene->CreateNewEntity( "Folder" ) );
                    folder.AddComponent<ECS::FolderComponent>();
                    for ( const auto& id : Core::SelectionManager::GetSelection() )
                        if ( const auto e = scene->FindEntityByID( id ) )
                            scene->Attach( folder, e->get() );
                }

                if ( ImGui::BeginMenu( "Light" ) )
                {
                    if ( ImGui::Selectable( "Directional Light" ) )
                    {
                        auto entity = scene->CreateNewEntity( "Directional Light" );
                        entity.AddComponent<ECS::DirectionLightComponent>();
                        track( entity );
                    }
                    if ( ImGui::Selectable( "Point Light" ) )
                    {
                        auto entity = scene->CreateNewEntity( "Point Light" );
                        entity.AddComponent<ECS::PointLightComponent>();
                        track( entity );
                    }
                    if ( ImGui::Selectable( "Spot Light" ) )
                    {
                        auto entity = scene->CreateNewEntity( "Spot Light" );
                        entity.AddComponent<ECS::SpotLightComponent>();
                        track( entity );
                    }
                    ImGui::EndMenu();
                }

                if ( ImGui::Selectable( "Skybox" ) )
                    track( scene->CreateNewEntity( "Skybox" ) ).AddComponent<ECS::SkyboxComponent>();

                if ( ImGui::Selectable( "Terrain" ) )
                    track( scene->CreateNewEntity( "Terrain" ) ).AddComponent<ECS::TerrainComponent>();

                // NOTE: no standalone "Material" entity — a MaterialComponent is meaningless without
                // geometry. It is added ONTO a renderable entity via Details -> Add Component.

                if ( ImGui::Selectable( "3D Model" ) )
                {
                    track( scene->CreateNewEntity( "3D Model" ) )
                         .AddComponent<ECS::StaticMeshComponent>()
                         .MeshHandle = Assets::AssetHandle{ 0 };
                }

                if ( ImGui::Selectable( "Skinned Model" ) )
                {
                    auto entity = scene->CreateNewEntity( "Skinned Model" );
                    entity.AddComponent<ECS::SkinnedMeshComponent>();
                    entity.AddComponent<ECS::AnimationComponent>();
                    track( entity );
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
                    track( entity );
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
                    track( camEntity );
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
                    std::vector<Common::UUID> created; // the whole box = ONE undo step

                    // UE-style: each wall gets a real, shared material ASSET in its slot (not a per-entity
                    // override) — so it shows up in 3D Model -> Material and is editable/reusable.
                    auto mkBox = [&]( const char* name, glm::vec3 pos, glm::vec3 scale, const glm::vec4& albedo,
                                      const std::string& matName )
                    {
                        auto  e  = scene->CreateNewEntity( name );
                        created.push_back( e.GetComponent<ECS::UUIDComponent>().UUID );
                        auto& smc     = e.AddComponent<ECS::StaticMeshComponent>();
                        smc.Primitive = Geometry::PrimitiveType::Cube;
                        if ( auto mat = CreatePBRMaterialAsset( assetManager.get(), matName, albedo, 0.9f ) )
                            smc.MaterialSlots.push_back( mat );
                        auto& tf       = e.GetComponent<ECS::TransformComponent>();
                        tf.Translation = pos;
                        tf.Scale       = scale;
                    };
                    const glm::vec4 white( 0.82f, 0.82f, 0.80f, 1.0f );
                    const glm::vec4 red( 0.85f, 0.10f, 0.10f, 1.0f );
                    const glm::vec4 green( 0.10f, 0.70f, 0.15f, 1.0f );
                    mkBox( "CB_Floor", { 0.0f, 0.0f, 0.0f }, { 4.0f, 0.1f, 4.0f }, white, "CB_White" );
                    mkBox( "CB_Back", { 0.0f, 2.0f, -2.0f }, { 4.0f, 4.0f, 0.1f }, white, "CB_White" );
                    mkBox( "CB_LeftRed", { -2.0f, 2.0f, 0.0f }, { 0.1f, 4.0f, 4.0f }, red, "CB_Red" );
                    mkBox( "CB_RightGreen", { 2.0f, 2.0f, 0.0f }, { 0.1f, 4.0f, 4.0f }, green, "CB_Green" );

                    auto mkWhite = [&]( const char* name, Geometry::PrimitiveType prim, glm::vec3 pos, glm::vec3 scale )
                    {
                        auto  e  = scene->CreateNewEntity( name );
                        created.push_back( e.GetComponent<ECS::UUIDComponent>().UUID );
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
                        created.push_back( sun.GetComponent<ECS::UUIDComponent>().UUID );
                        sun.AddComponent<ECS::DirectionLightComponent>();
                        sun.GetComponent<ECS::TransformComponent>().Translation =
                             glm::normalize( glm::vec3( -0.6f, -1.0f, -0.2f ) );
                    }

                    Commands::NotifyCreated( created );
                }

                if ( ImGui::Selectable( "LOD Test Grid (HD spheres)" ) )
                {
                    // One high-poly sphere (with a LOD chain) shared by a row of entities receding from
                    // the camera — pull back and the far ones simplify. Toggle Scene Settings > Debug >
                    // "Mesh LOD (auto)". LOD0 (near) is identical geometry.
                    constexpr uint32_t sectors = 96, stacks = 48;
                    constexpr float    radius  = 0.5f;

                    std::vector<::Desert::Vertex> verts;
                    std::vector<::Desert::Index>  inds;
                    for ( uint32_t i = 0; i <= stacks; ++i )
                    {
                        const float phi = glm::pi<float>() * float( i ) / float( stacks );
                        const float sp = std::sin( phi ), cp = std::cos( phi );
                        for ( uint32_t j = 0; j <= sectors; ++j )
                        {
                            const float     th = glm::two_pi<float>() * float( j ) / float( sectors );
                            const float     st = std::sin( th ), ct = std::cos( th );
                            const glm::vec3 n = { sp * ct, cp, sp * st };
                            const glm::vec3 tg = { -st, 0.0f, ct };
                            verts.push_back( { n * radius, n, tg, glm::cross( n, tg ),
                                               { float( j ) / sectors, float( i ) / stacks } } );
                        }
                    }
                    const uint32_t stride = sectors + 1;
                    for ( uint32_t i = 0; i < stacks; ++i )
                        for ( uint32_t j = 0; j < sectors; ++j )
                        {
                            const uint32_t a = i * stride + j, b = a + stride;
                            inds.push_back( { a, a + 1, b } );
                            inds.push_back( { a + 1, b + 1, b } );
                        }
                    Common::Math::AABB aabb;
                    aabb.Min = glm::vec3( -radius );
                    aabb.Max = glm::vec3( radius );
                    std::vector<::Desert::Submesh> subs = { { "Sphere", 0, uint32_t( verts.size() ), 0,
                                                             uint32_t( inds.size() ) * 3, glm::mat4( 1.0f ),
                                                             aabb } };

                    auto sphere = std::make_shared<DynamicMesh>( verts, inds, subs, /*generateLODs*/ true );
                    sphere->Invalidate(); // build GPU buffers (base + LOD indices)

                    // One shared material ASSET for the whole row (UE-style slot assignment), so the spheres
                    // read as a real, editable material instead of per-entity overrides.
                    const auto lodMat =
                         CreatePBRMaterialAsset( assetManager.get(), "LOD_Sphere", glm::vec4( 0.70f, 0.75f, 0.85f, 1.0f ),
                                                 0.5f );

                    std::vector<Common::UUID> created;
                    for ( int k = 0; k < 8; ++k )
                    {
                        auto e = scene->CreateNewEntity( "LOD_Sphere_" + std::to_string( k ) );
                        created.push_back( e.GetComponent<ECS::UUIDComponent>().UUID );
                        auto& smc      = e.AddComponent<ECS::StaticMeshComponent>();
                        smc.RuntimeMesh = sphere;
                        if ( lodMat )
                            smc.MaterialSlots.push_back( lodMat );
                        auto& tf       = e.GetComponent<ECS::TransformComponent>();
                        tf.Translation = { float( k ) * 4.0f - 8.0f, 1.0f, -float( k ) * 10.0f };
                    }
                    if ( scene->GetRegistry().view<ECS::DirectionLightComponent>().size() == 0 )
                    {
                        auto sun = scene->CreateNewEntity( "LOD_Sun" );
                        created.push_back( sun.GetComponent<ECS::UUIDComponent>().UUID );
                        sun.AddComponent<ECS::DirectionLightComponent>();
                        sun.GetComponent<ECS::TransformComponent>().Translation =
                             glm::normalize( glm::vec3( -0.4f, -1.0f, -0.5f ) );
                    }
                    Commands::NotifyCreated( created );
                }

                if ( ImGui::BeginMenu( "Primitive" ) )
                {
                    if ( ImGui::MenuItem( "Cube" ) )
                    {
                        // Use the Primitive path (RuntimeMesh generated + Invalidated by MeshECSSystem) —
                        // it renders + serializes reliably, unlike the builtin procedural-handle path.
                        auto& cubeMesh =
                             track( scene->CreateNewEntity( "Cube" ) ).AddComponent<ECS::StaticMeshComponent>();
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
            AddEntity( m_Scene, m_AssetManager );
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
            AddEntity( m_Scene, m_AssetManager );
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

        if ( m_OpenSavePrefab )
        {
            ImGui::OpenPopup( "SavePrefabPopup" );
            m_OpenSavePrefab = false;
        }
        DrawSavePrefabPopup();

        if ( m_OpenApplyPrefab )
        {
            ImGui::OpenPopup( "ApplyPrefabPopup" );
            m_OpenApplyPrefab = false;
        }
        if ( ImGui::BeginPopupModal( "ApplyPrefabPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
        {
            ImGui::TextUnformatted( "Overwrite the source .deprefab file with this instance's current state?" );
            ImGui::TextDisabled( "Other instances pick the changes up when they are (re)instantiated." );
            ImGui::Spacing();
            if ( ImGui::Button( "Apply", ImVec2( 120, 0 ) ) )
            {
                if ( m_ApplyPrefabTarget )
                    Commands::ApplyPrefabInstance( *m_ApplyPrefabTarget ); // file write only (safe here)
                m_ApplyPrefabTarget.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if ( ImGui::Button( "Cancel", ImVec2( 90, 0 ) ) )
            {
                m_ApplyPrefabTarget.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Keyboard (outliner focused, not while typing in a field):
        //   Delete — remove the whole selection; F2 — rename the primary; Esc — clear the selection.
        if ( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) && !ImGui::IsAnyItemActive() )
        {
            if ( ImGui::IsKeyPressed( ImGuiKey_Delete, false ) && Core::SelectionManager::Count() > 0 )
                m_PendingDelete = Core::SelectionManager::GetSelection();

            if ( ImGui::IsKeyPressed( ImGuiKey_F2, false ) )
            {
                if ( const auto& sel = Core::SelectionManager::GetSelected() )
                    if ( auto ref = m_Scene->FindEntityByID( *sel ) )
                    {
                        m_RenamingEntity     = *sel;
                        m_RenameBuffer       = ref->get().GetComponent<ECS::TagComponent>().Tag;
                        m_RenameFocusPending = true;
                    }
            }

            if ( ImGui::IsKeyPressed( ImGuiKey_Escape, false ) && !m_RenamingEntity.has_value() )
                Core::SelectionManager::ClearSelection();
        }

        // Entity / selection counters (compact, above the table).
        {
            const size_t total = m_Scene->GetAllEntities().size();
            const size_t sel   = Core::SelectionManager::Count();
            if ( sel > 0 )
                ImGui::TextDisabled( "%zu entities, %zu selected", total, sel );
            else
                ImGui::TextDisabled( "%zu entities", total );
        }

        // Entity table
        {
            // Resizable: the Name|Type divider is draggable (long component-type names were unreadable
            // behind the old fixed 110px column).
            constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                                   ImGuiTableFlags_SizingStretchProp |
                                                   ImGuiTableFlags_Resizable;

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
                    // Drop on empty space = unparent to scene root (undoable, applied after iteration).
                    Common::UUID uuid = *(const Common::UUID*)payload->Data;
                    m_PendingReparent = { Core::SelectionManager::IsSelected( uuid )
                                               ? Core::SelectionManager::GetSelection()
                                               : std::vector<Common::UUID>{ uuid },
                                          Common::UUID::Null() };
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
                            auto root = prefabAsset->Instantiate( m_Scene.get(), *m_AssetManager, nullptr );
                            if ( root )
                                Commands::NotifyCreated( { root.GetComponent<ECS::UUIDComponent>().UUID } );
                        }
                    }
                }

                ImGui::EndDragDropTarget();
            }
        }

        // Process deferred structural edits AFTER the entt view iteration above (they create/destroy
        // entities or edit the Children vectors the tree walk was iterating).
        if ( !m_PendingDelete.empty() )
        {
            Commands::DeleteEntities( m_PendingDelete ); // one undo step restores everything
            m_PendingDelete.clear();
        }
        if ( !m_PendingDuplicate.empty() )
        {
            if ( auto dups = Commands::DuplicateEntities( m_PendingDuplicate ); !dups.empty() )
                Core::SelectionManager::SetSelection( std::move( dups ) );
            m_PendingDuplicate.clear();
        }
        if ( m_PendingReparent.has_value() )
        {
            Commands::ReparentMany( m_PendingReparent->first, m_PendingReparent->second );
            m_PendingReparent.reset();
        }
        if ( m_PendingPrefabRevert.has_value() )
        {
            Commands::RevertPrefabInstance( *m_PendingPrefabRevert ); // one undo step
            m_PendingPrefabRevert.reset();
        }
    }

    void SceneHierarchyPanel::DrawSavePrefabPopup()
    {
        if ( !ImGui::BeginPopupModal( "SavePrefabPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
            return;

        static std::string s_error;

        ImGui::TextUnformatted( "Save entity (with its whole subtree) as a prefab:" );
        ImGui::SetNextItemWidth( 460.0f );
        Utils::ImGuiUtilities::InputText( m_SavePrefabPath, "##SavePrefabPath" );
        ImGui::TextDisabled( "An existing file at this path will be overwritten." );

        if ( !s_error.empty() )
            ImGui::TextColored( ImVec4( 1.0f, 0.4f, 0.4f, 1.0f ), "%s", s_error.c_str() );

        ImGui::Spacing();

        if ( ImGui::Button( "Save", ImVec2( 120, 0 ) ) && !m_SavePrefabPath.empty() )
        {
            namespace fs = std::filesystem;
            const bool rightExt = fs::path( m_SavePrefabPath ).extension() ==
                                  Common::Constants::Extensions::PREFAB_EXTENSION;
            std::optional<std::reference_wrapper<const ECS::Entity>> entityRef;
            if ( m_SavePrefabTarget )
                entityRef = m_Scene->FindEntityByID( *m_SavePrefabTarget );

            if ( !rightExt )
            {
                s_error = "Path must end with .deprefab";
            }
            else if ( !entityRef || !m_AssetManager )
            {
                s_error = "Entity no longer exists.";
            }
            else
            {
                std::error_code ec;
                fs::create_directories( fs::path( m_SavePrefabPath ).parent_path(), ec );

                auto prefabAsset = m_AssetManager->FindByPath<Assets::PrefabAsset>( m_SavePrefabPath );
                if ( !prefabAsset )
                    prefabAsset = m_AssetManager->CreateAsset<Assets::PrefabAsset>(
                         Assets::AssetPriority::High, m_SavePrefabPath,
                         /*loadAfterCreate=*/false ); // the file does not exist yet — we are creating it

                if ( !prefabAsset )
                {
                    s_error = "Could not create the prefab asset.";
                }
                else
                {
                    ECS::Entity root = entityRef->get();
                    prefabAsset->CreateFromEntity( root, *m_AssetManager );
                    Common::Utils::FileSystem::WriteContentToFile( Common::Filepath( m_SavePrefabPath ),
                                                                   prefabAsset->Serialize() );

                    // Mark the live entity as an instance of the prefab it was just saved as.
                    if ( !root.HasComponent<ECS::PrefabComponent>() )
                        root.AddComponent<ECS::PrefabComponent>();
                    root.GetComponent<ECS::PrefabComponent>().Prefab = prefabAsset->GetMetadata().Handle;

                    LOG_INFO( "[Prefab] Saved '{}' -> {}",
                              root.GetComponent<ECS::TagComponent>().Tag, m_SavePrefabPath );
                    s_error.clear();
                    ImGui::CloseCurrentPopup();
                }
            }
        }

        ImGui::SameLine();
        if ( ImGui::Button( "Cancel", ImVec2( 90, 0 ) ) )
        {
            s_error.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
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
                        auto root = prefabAsset->Instantiate( m_Scene.get(), *m_AssetManager, nullptr );
                        if ( root )
                            Commands::NotifyCreated( { root.GetComponent<ECS::UUIDComponent>().UUID } );
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
