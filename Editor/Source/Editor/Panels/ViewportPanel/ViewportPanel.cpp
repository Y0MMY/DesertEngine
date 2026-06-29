#include "ViewportPanel.hpp"

#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Editor/Core/Selection/SkeletonEditMode.hpp>
#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Panels/MeshEditor/MeshEditorPanel.hpp>
#include <Editor/Import/MeshDnD.hpp>
#include <filesystem>
#include <Engine/Geometry/DynamicMesh.hpp>
#include <Engine/Geometry/PrimitiveMeshFactory.hpp>
#include <Engine/Geometry/SkinnedMesh.hpp>
#include <Engine/Animation/Skeleton.hpp>
#include <functional>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Prefab/PrefabAsset.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Core/Formats/ImageFormat.hpp>
#include <Common/Core/Math/Ray.hpp>

#include <ImGuizmo.h>
#include <glm/gtx/matrix_decompose.hpp>

#include <cmath>
#include <algorithm>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;
    ViewportPanel::ViewportPanel( const std::shared_ptr<Desert::Core::Scene>& scene,
                                  const Assets::AssetManager*                 assetManager )
         : IPanel( "Scene###scene" ), m_Scene( scene ), m_AssetManager( assetManager )
    {
        m_UIHelper = std::make_unique<Editor::UI::UIHelper>();
        m_UIHelper->Init();

        m_LightGizmoRenderer = std::make_unique<LightGizmoRenderer>( scene );
    }

    void ViewportPanel::OnUIRender()
    {
        const auto& mainCamera = m_Scene->GetMainCamera().lock();
        if ( !mainCamera )
        {
            ImGui::TextColored( ImVec4( 1.0f, 0.4f, 0.4f, 1.0f ), "Camera was not found" );
            ImGui::TextWrapped( "Please add a camera to the scene to display the view." );
            return;
        }

        ImVec2 mousePos    = ::ImGui::GetMousePos();
        ImVec2 viewportPos = ::ImGui::GetWindowPos();

        ImVec2 viewportMin = ImGui::GetWindowPos();
        viewportMin.x += ImGui::GetWindowContentRegionMin().x;
        viewportMin.y += ImGui::GetWindowContentRegionMin().y;

        ImVec2 viewportMax = ImGui::GetWindowPos();
        viewportMax.x += ImGui::GetWindowContentRegionMax().x;
        viewportMax.y += ImGui::GetWindowContentRegionMax().y;

        m_ViewportData.ViewportPos = { ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMin().x,
                                       ImGui::GetWindowPos().y + ImGui::GetWindowContentRegionMin().y };

        m_ViewportData.MousePosition = glm::vec2( mousePos.x - viewportMin.x, mousePos.y - viewportMin.y );
        const auto oldSize           = m_ViewportData.Size;

        m_ViewportData.Size      = { viewportMax.x - viewportMin.x, viewportMax.y - viewportMin.y };
        m_ViewportData.IsHovered = ::ImGui::IsWindowHovered();

        if ( oldSize != m_ViewportData.Size )
        {
            // Store the new size and apply it in OnPreUpdate() next frame, before any recording
            // starts. Calling Scene::Resize() here (inside OnImGuiRender) destroys descriptor set
            // pools while their DS are still bound to the recording command buffer.
            m_PendingViewportSize = m_ViewportData.Size;
            mainCamera->UpdateProjectionMatrix( m_ViewportData.Size.x,
                                                m_ViewportData.Size.y ); // TODO: Move to scene
        }

        m_ViewportData.IsHovered = ImGui::IsWindowHovered();

        // Render scene
        m_UIHelper->Image( m_Scene->GetFinalImage(), { m_ViewportData.Size.x, m_ViewportData.Size.y } );

        // Blender-style mode toggle (Object/Scene <-> Skeleton Edit). Drawn as a FLOATING overlay window
        // anchored to the viewport's top-left so it sits ON TOP of the viewport image and never slips behind
        // the editor toolbar. Only meaningful for a selected skinned mesh; disabled otherwise.
        {
            bool canEditSkeleton = false;
            if ( const auto& sel = Core::SelectionManager::GetSelected(); sel.has_value() )
                if ( auto ref = m_Scene->FindEntityByID( *sel ); ref )
                    canEditSkeleton = ref->get().HasComponent<ECS::SkinnedMeshComponent>();

            if ( !canEditSkeleton )
                Core::SkeletonEditMode::SetActive( false ); // auto-exit when the selection isn't a skinned mesh

            const bool active = Core::SkeletonEditMode::IsActive();

            ImGui::SetNextWindowPos( ImVec2( m_ViewportData.ViewportPos.x + 10.0f,
                                             m_ViewportData.ViewportPos.y + 10.0f ) );
            ImGui::SetNextWindowBgAlpha( 0.55f );
            const ImGuiWindowFlags overlayFlags =
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
            if ( ImGui::Begin( "##ViewportModeOverlay", nullptr, overlayFlags ) )
            {
                if ( !canEditSkeleton )
                    ImGui::BeginDisabled();
                if ( active )
                    ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.85f, 0.45f, 0.1f, 1.0f ) );
                if ( ImGui::Button( active ? ICON_MDI_BONE "  Skeleton Edit" : ICON_MDI_HUMAN "  Object Mode" ) )
                    Core::SkeletonEditMode::Toggle();
                if ( active )
                    ImGui::PopStyleColor();
                if ( !canEditSkeleton )
                    ImGui::EndDisabled();

                // In Skeleton Edit, a toggle for showing ALL bone names (default: only the selected bone, so
                // dense rigs don't overlap every label into an unreadable blob).
                if ( active )
                {
                    ImGui::SameLine();
                    bool showNames = Core::SkeletonEditMode::ShowAllNames();
                    if ( ImGui::Checkbox( "Names", &showNames ) )
                        Core::SkeletonEditMode::SetShowAllNames( showNames );
                }

                // Editor fly-camera speed (only while the active camera IS the editor camera, i.e. not Play).
                if ( auto cam = m_Scene->GetMainCamera().lock() )
                {
                    if ( auto* editorCam = dynamic_cast<::Desert::Core::EditorCamera*>( cam.get() ) )
                    {
                        float spd = editorCam->GetMovementSpeed();
                        ImGui::SetNextItemWidth( 110.0f );
                        if ( ImGui::SliderFloat( ICON_MDI_CAMERA "##CamSpeed", &spd, 0.1f, 10.0f, "%.2fx" ) )
                            editorCam->SetMovementSpeed( spd );
                        if ( ImGui::IsItemHovered() )
                            ImGui::SetTooltip( "Editor camera speed" );
                    }
                }
            }
            ImGui::End();
        }

        // Drag a prefab file from the File Explorer onto the viewport to instantiate it into the scene.
        if ( ImGui::BeginDragDropTarget() )
        {
            if ( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( "PREFAB_FILE" ); payload && m_AssetManager )
            {
                const std::string path( static_cast<const char*>( payload->Data ),
                                        payload->DataSize > 0 ? payload->DataSize - 1 : 0 );
                auto& mgr = const_cast<Assets::AssetManager&>( *m_AssetManager );
                auto  prefab = mgr.FindByPath<Assets::PrefabAsset>( path );
                if ( !prefab )
                    prefab = mgr.CreateAsset<Assets::PrefabAsset>( Assets::AssetPriority::High, path );
                if ( prefab )
                {
                    if ( !prefab->IsReadyForUse() )
                        prefab->Load();
                    prefab->Instantiate( m_Scene.get(), *m_AssetManager, nullptr );
                }
            }

            // Drag a mesh source (.obj/.fbx/.gltf/...) from the File Explorer onto the viewport to spawn it
            // as a new entity. Cooks the source on demand (see MeshDnD).
            if ( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( "MESH_ASSET" ); payload && m_AssetManager )
            {
                const std::string path( static_cast<const char*>( payload->Data ),
                                        payload->DataSize > 0 ? payload->DataSize - 1 : 0 );
                auto&      mgr    = const_cast<Assets::AssetManager&>( *m_AssetManager );
                const auto handle = MeshDnD::ResolveOrImport( mgr, path );
                if ( !handle.IsNull() )
                {
                    const std::string name = std::filesystem::path( path ).stem().string();
                    auto&             e    = m_Scene->CreateNewEntity( std::string( name ) );
                    e.AddComponent<ECS::StaticMeshComponent>().MeshHandle = handle;
                    Core::SelectionManager::SetSelected( e.GetComponent<ECS::UUIDComponent>().UUID );
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Terrain splat painting: when a terrain entity is selected, show the brush overlay; with the brush
        // enabled, LMB-drag paints into the splat map (and suppresses the object gizmo to avoid conflicts).
        const ECS::Entity* terrainEntity = nullptr;
        if ( const auto& sel = Core::SelectionManager::GetSelected(); sel.has_value() )
        {
            if ( auto ref = m_Scene->FindEntityByID( *sel ); ref )
            {
                const ECS::Entity& e = ref->get();
                if ( e.HasComponent<ECS::TerrainComponent>() )
                    terrainEntity = &e;
            }
        }
        if ( terrainEntity )
            DrawTerrainPaintOverlay( *terrainEntity );

        const bool painting = terrainEntity && m_TerrainBrush.Enabled;

        // Handle gizmos
        m_GizmoHovered = false;
        if ( Core::SkeletonEditMode::IsActive() )
        {
            RenderBoneGizmo(); // skeleton edit owns the gizmo (edits the selected bone, not the object)
        }
        else if ( m_GizmoType != GizmoType::None && !painting )
        {
            RenderGizmo();
        }

        if ( painting && m_ViewportData.IsHovered )
        {
            // Replace the OS pointer with the brush: hide the arrow and draw the world-space radius ring.
            ImGui::SetMouseCursor( ImGuiMouseCursor_None );
            DrawBrushRing( *terrainEntity );

            if ( ImGui::IsMouseDown( ImGuiMouseButton_Left ) &&
                 !ImGui::IsAnyItemActive() ) // don't paint while dragging the brush sliders
                PaintTerrainAtCursor( *terrainEntity );
        }

        // Editor gizmos (light/camera icons + frustums) are authoring aids — hide them in Play/Paused so the
        // running game view is clean.
        if ( m_Scene->GetState() == ::Desert::Core::Scene::SceneState::Edit )
            m_LightGizmoRenderer->Render( m_ViewportData.Size.x, m_ViewportData.Size.y,
                                          m_ViewportData.ViewportPos.x, m_ViewportData.ViewportPos.y );
    }

    void ViewportPanel::OnPreUpdate()
    {
        if ( m_PendingViewportSize.has_value() )
        {
            m_Scene->Resize( (uint32_t)m_PendingViewportSize->x, (uint32_t)m_PendingViewportSize->y );
            m_PendingViewportSize.reset();
        }

        // Re-upload edited splat maps here (before any recording) so we never release a GPU image that is
        // still bound to an in-flight command buffer.
        UploadDirtySplatMaps();
    }

    void ViewportPanel::RenderGizmo()
    {
        // NOTE: ImGuizmo::BeginFrame() is issued once per frame by EditorLayer, before any panel runs.
        const auto& mainCamera = m_Scene->GetMainCamera().lock();
        if ( !mainCamera )
            return;

        const auto& selected = Core::SelectionManager::GetSelected();
        if ( !selected )
            return;

        const auto& selectedEntityOpt = m_Scene->FindEntityByID( *selected );
        if ( !selectedEntityOpt )
            return;

        auto& selectedEntity = selectedEntityOpt->get();

        // If the Mesh Editor is open and editing this entity, vertex editing owns the (global) ImGuizmo
        // interaction — drawing the object gizmo here would steal the drag and move the whole object.
        if ( auto* meshEditor = MeshEditorPanel::GetInstance();
             meshEditor && meshEditor->IsActivelyEditing( selectedEntity ) )
            return;

        auto& transformComponent = selectedEntity.GetComponent<ECS::TransformComponent>();

        // The gizmo must work in WORLD space. For a CHILD entity (e.g. a camera parented to the character),
        // the world transform = parentWorld * local, and an edit must be converted back to LOCAL before
        // writing. parentWorld = identity for a root entity (so this is a no-op there).
        glm::mat4  parentWorld( 1.0f );
        auto&      reg  = m_Scene->GetRegistry();
        const auto self = selectedEntity.GetHandle();
        if ( reg.has<ECS::RelationshipComponent>( self ) )
        {
            std::vector<entt::entity> chain; // [parent, grandparent, ... root]
            entt::entity              cur = reg.get<ECS::RelationshipComponent>( self ).Parent;
            while ( cur != entt::null )
            {
                chain.push_back( cur );
                cur = reg.has<ECS::RelationshipComponent>( cur ) ? reg.get<ECS::RelationshipComponent>( cur ).Parent
                                                                 : entt::null;
            }
            for ( auto it = chain.rbegin(); it != chain.rend(); ++it ) // root -> ... -> parent
                if ( reg.has<ECS::TransformComponent>( *it ) )
                    parentWorld = parentWorld * reg.get<ECS::TransformComponent>( *it ).GetTransform();
        }

        auto modelMatrix = parentWorld * transformComponent.GetTransform(); // world transform for the gizmo

        // SetRect MUST match the rendered scene-image rect (content region), NOT the raw window rect —
        // GetWindowPos()/GetWindowWidth() include the tab bar + padding, which would offset the gizmo from
        // the object and throw the handle hit-test off (gizmo "drawn wrong" / won't grab). The image is drawn
        // at m_ViewportData.ViewportPos with m_ViewportData.Size (see the Image() call), so use those.
        ImGuizmo::SetOrthographic( false );
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect( m_ViewportData.ViewportPos.x, m_ViewportData.ViewportPos.y, m_ViewportData.Size.x,
                           m_ViewportData.Size.y );

        const auto& view = mainCamera->GetViewMatrix();
        const auto& proj = mainCamera->GetProjectionMatrix();

        // Vertex-level editing now lives entirely in the Mesh Editor panel's own viewport; the main
        // viewport only manipulates whole objects.

        // --- STANDARD OBJECT GIZMO ---
        if ( ImGuizmo::Manipulate( &view[0][0], &proj[0][0], static_cast<ImGuizmo::OPERATION>( m_GizmoType ),
                                   ImGuizmo::WORLD, &modelMatrix[0][0] ) )
        {
            if ( ImGuizmo::IsOver() )
            {
                m_GizmoHovered = true;
            }

            // Convert the manipulated WORLD matrix back to the entity's LOCAL space (inverse parent) before
            // decomposing — so dragging a child entity edits its local offset correctly.
            const glm::mat4 localMatrix = glm::inverse( parentWorld ) * modelMatrix;

            glm::vec3 scale, translation, skew;
            glm::quat rotation;
            glm::vec4 perspective;
            glm::decompose( localMatrix, scale, rotation, translation, skew, perspective );

            transformComponent.Translation = translation;
            transformComponent.Rotation    = glm::eulerAngles( rotation );
            transformComponent.Scale       = scale;
        }
    }

    void ViewportPanel::RenderBoneGizmo()
    {
        const auto& mainCamera = m_Scene->GetMainCamera().lock();
        if ( !mainCamera )
            return;

        const int boneIdx = Core::SkeletonEditMode::GetSelectedBone();
        if ( boneIdx < 0 )
            return;

        const auto& selected = Core::SelectionManager::GetSelected();
        if ( !selected )
            return;
        const auto& entOpt = m_Scene->FindEntityByID( *selected );
        if ( !entOpt )
            return;
        auto& entity = entOpt->get();
        if ( !entity.HasComponent<ECS::SkinnedMeshComponent>() )
            return;

        auto& smc  = entity.GetComponent<ECS::SkinnedMeshComponent>();
        auto* mesh = Runtime::ResourceRegistry::GetMeshService()->Get( smc.MeshHandle );
        if ( !mesh || !mesh->IsSkinned() )
            return;
        auto* skeleton = static_cast<SkinnedMesh*>( mesh )->GetSkeletonMutable();
        auto& bones    = skeleton->GetBonesMutable();
        if ( boneIdx >= static_cast<int>( bones.size() ) )
            return;

        const glm::mat4 entityWorld = entity.GetComponent<ECS::TransformComponent>().GetTransform();

        // Chain global bind per bone (parent chain of LocalBindTransform) — the SAME space the mesh is
        // skinned in (bind matrix = chainGlobal * OffsetMatrix), so the gizmo sits on the bone as rendered.
        std::vector<glm::mat4>             chainGlobal( bones.size(), glm::mat4( 1.0f ) );
        std::vector<bool>                  done( bones.size(), false );
        std::function<glm::mat4( size_t )> resolve = [&]( size_t i ) -> glm::mat4
        {
            if ( done[i] )
                return chainGlobal[i];
            glm::mat4 g = bones[i].LocalBindTransform;
            if ( bones[i].ParentBoneID.has_value() && bones[i].ParentBoneID.value() < bones.size() )
                g = resolve( bones[i].ParentBoneID.value() ) * bones[i].LocalBindTransform;
            chainGlobal[i] = g;
            done[i]        = true;
            return g;
        };

        glm::mat4 gizmoWorld = entityWorld * resolve( static_cast<size_t>( boneIdx ) );

        ImGuizmo::SetOrthographic( false );
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect( m_ViewportData.ViewportPos.x, m_ViewportData.ViewportPos.y, m_ViewportData.Size.x,
                           m_ViewportData.Size.y );

        const auto& view = mainCamera->GetViewMatrix();
        const auto& proj = mainCamera->GetProjectionMatrix();
        // Scale on a bone's rest pose is rarely wanted; default None/Scale to Translate.
        const auto op = ( m_GizmoType == GizmoType::Rotate ) ? ImGuizmo::ROTATE : ImGuizmo::TRANSLATE;

        if ( ImGuizmo::Manipulate( &view[0][0], &proj[0][0], op, ImGuizmo::WORLD, &gizmoWorld[0][0] ) )
        {
            m_GizmoHovered = ImGuizmo::IsOver();

            // Edit ONLY this bone's LocalBindTransform (relative to its parent's unchanged chain global).
            // Descendants follow automatically (their local is unchanged → their chain global shifts with the
            // parent), and the mesh follows too (it is skinned with chainGlobal * OffsetMatrix). OffsetMatrix
            // is the imported inverse-bind and is intentionally LEFT ALONE.
            const glm::mat4 newGlobalMesh = glm::inverse( entityWorld ) * gizmoWorld;
            glm::mat4       parentGlobal( 1.0f );
            if ( bones[boneIdx].ParentBoneID.has_value() &&
                 bones[boneIdx].ParentBoneID.value() < bones.size() )
                parentGlobal = resolve( bones[boneIdx].ParentBoneID.value() );
            bones[boneIdx].LocalBindTransform = glm::inverse( parentGlobal ) * newGlobalMesh;
        }
    }

    std::pair<float, float> ViewportPanel::GetMouseViewportSpace() const
    {
        return { m_ViewportData.MousePosition.x, m_ViewportData.MousePosition.y };
    }

    void ViewportPanel::HandleObjectPicking()
    {
        const auto& mainCamera = m_Scene->GetMainCamera().lock();
        if ( !mainCamera )
        {
            return;
        }

        if ( m_GizmoHovered )
        {
            return;
        }

        // not over viewport
        if ( !m_ViewportData.IsHovered )
        {
            return;
        }
        auto [mouseX, mouseY] = GetMouseViewportSpace();
        const auto ray        = Common::Math::Ray::FromScreenPosition(
             { mouseX, mouseY }, mainCamera->GetProjectionMatrix(), mainCamera->GetViewMatrix(),
             mainCamera->GetPosition(), static_cast<uint32_t>( m_ViewportData.Size.x ),
             static_cast<uint32_t>( m_ViewportData.Size.y ) );

        float        closestT = std::numeric_limits<float>::max();
        Common::UUID selectedUUID;

        const auto entities = m_Scene->GetAllEntities();
        auto&      registry = m_Scene->GetRegistry();

        std::vector<std::pair<Common::UUID, std::pair<glm::mat4, Desert::Mesh*>>> allMeshes;

        for ( const auto& entity : entities )
        {
            if ( entity.HasComponent<ECS::StaticMeshComponent>() )
            {
                const auto mesh = GetMeshComponent( entity.GetComponent<ECS::StaticMeshComponent>() );
                if ( !mesh )
                {
                    continue;
                }

                allMeshes.push_back( { entity.GetComponent<ECS::UUIDComponent>().UUID,
                                       { entity.GetWorldTransform(), mesh } } );
            }
        }

        for ( const auto& [uuid, meshData] : allMeshes )
        {
            const auto& [transform, mesh] = meshData;
            float t                       = 0.0f;
            auto  localRay                = ray.ToLocalSpace( transform );

            for ( const auto& submesh : mesh->GetSubmeshes() )
            {
                if ( localRay.IntersectsAABB( submesh.BoundingBox, t ) )
                {
                    if ( t < closestT )
                    {
                        selectedUUID = uuid;
                        closestT     = t;
                    }
                }
            }
        }

        if ( closestT != std::numeric_limits<float>::max() )
        {
            // If the hit entity is a child of a prefab, select the prefab root so the
            // entire prefab gets outlined instead of just one submesh.
            auto hitEntityRef = m_Scene->FindEntityByID( selectedUUID );
            if ( hitEntityRef )
            {
                const ECS::Entity& hitEntity = hitEntityRef->get();
                if ( !hitEntity.HasComponent<ECS::PrefabComponent>() )
                {
                    entt::entity current = hitEntity.GetHandle();

                    while ( registry.has<ECS::RelationshipComponent>( current ) )
                    {
                        const auto& rel = registry.get<ECS::RelationshipComponent>( current );
                        if ( rel.Parent == entt::null )
                            break;
                        current = rel.Parent;
                        if ( registry.has<ECS::PrefabComponent>( current ) &&
                             registry.has<ECS::UUIDComponent>( current ) )
                        {
                            selectedUUID = registry.get<ECS::UUIDComponent>( current ).UUID;
                            break;
                        }
                    }
                }
            }

            Core::SelectionManager::SetSelected( selectedUUID );
        }
    }

    void ViewportPanel::OnEvent( Common::Event& e )
    {
        Common::EventManager eventManager( e );
        eventManager.Notify<Common::EventWindowResize>( [this]( Common::EventWindowResize& e )
                                                        { return OnWindowResize( e ); } );

        eventManager.Notify<Common::MouseButtonPressedEvent>( [this]( Common::MouseButtonPressedEvent& e )
                                                              { return OnMousePressed( e ); } );

        eventManager.Notify<Common::KeyPressedEvent>( [this]( Common::KeyPressedEvent& e )
                                                      { return OnKeyPressedEvent( e ); } );
    }

    bool ViewportPanel::OnWindowResize( Common::EventWindowResize& e )

    {
        // m_ImGuiLayer->Resize( e.width, e.height );
        // m_EditorCamera.UpdateProjectionMatrix( e.width, e.height );

        return false;
    }

    bool ViewportPanel::OnMousePressed( Common::MouseButtonPressedEvent& e )
    {
        // While the terrain brush is active, LMB paints (handled in OnUIRender) — don't also pick/select.
        if ( e.GetMouseButton() == Common::MouseButton::Left && !m_TerrainBrush.Enabled )
        {
            HandleObjectPicking();
        }

        return false;
    }

    bool ViewportPanel::OnKeyPressedEvent( Common::KeyPressedEvent& e )
    {
        switch ( e.GetKeyCode() )
        {
            case Common::KeyCode::Escape:
                m_GizmoType = GizmoType::None;
                break;
            case Common::KeyCode::T:
                m_GizmoType = GizmoType::Translate;
                break;
            case Common::KeyCode::R:
                m_GizmoType = GizmoType::Rotate;
                break;
            case Common::KeyCode::C:
                m_GizmoType = GizmoType::Scale;
                break;
        }
        return false;
    }

    Desert::Mesh* ViewportPanel::GetMeshComponent( const ECS::StaticMeshComponent& component )
    {
        if ( component.MeshHandle )
            return Runtime::ResourceRegistry::GetMeshService()->Get( component.MeshHandle );
        if ( component.RuntimeMesh )
            return component.RuntimeMesh.get(); // edited geometry (per-entity)
        if ( component.Primitive.has_value() )
            return Geometry::PrimitiveMeshFactory::GetShared( component.Primitive.value() ); // shared primitive
        return nullptr;
    }

    void ViewportPanel::DrawTerrainPaintOverlay( const ECS::Entity& terrainEntity )
    {
        auto& comp = terrainEntity.GetComponent<ECS::TerrainComponent>();

        // Floating overlay anchored to the viewport's top-left corner. Generous padding + a size that fits
        // all controls and the hint text without scrolling.
        ImGui::SetCursorPos( ImVec2( ImGui::GetWindowContentRegionMin().x + 12.0f,
                                     ImGui::GetWindowContentRegionMin().y + 12.0f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 12.0f, 12.0f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 8.0f, 8.0f ) );
        ImGui::BeginChild( "##TerrainPaint", ImVec2( 320.0f, 260.0f ), true );

        ImGui::TextUnformatted( "Terrain Paint" );
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Checkbox( "Enable Brush", &m_TerrainBrush.Enabled );

        const char* layers[] = { "Grass (R)", "Rock (G)", "Snow (B)" };
        ImGui::SetNextItemWidth( 180.0f );
        ImGui::Combo( "Layer", &m_TerrainBrush.Layer, layers, 3 );
        ImGui::SetNextItemWidth( 180.0f );
        ImGui::SliderFloat( "Radius", &m_TerrainBrush.Radius, 0.5f, comp.Data.Size, "%.1f m" );
        ImGui::SetNextItemWidth( 180.0f );
        ImGui::SliderFloat( "Strength", &m_TerrainBrush.Strength, 0.0f, 1.0f );
        ImGui::Checkbox( "Erase", &m_TerrainBrush.Erase );

        ImGui::Spacing();
        if ( ImGui::Button( "Clear Splat", ImVec2( 180.0f, 0.0f ) ) )
        {
            const uint32_t res = ECS::TerrainComponent::SplatResolution;
            comp.SplatPixels.assign( static_cast<size_t>( res ) * res * 4, 0 );
            comp.SplatDirty = true;
        }

        ImGui::Spacing();
        ImGui::TextDisabled( "Set the layer to 'Manual' in Details" );
        ImGui::TextDisabled( "to see painted weights. Hold LMB" );
        ImGui::TextDisabled( "and drag to paint." );

        ImGui::EndChild();
        ImGui::PopStyleVar( 2 );
    }

    bool ViewportPanel::TerrainPickPoint( const ECS::Entity& terrainEntity, glm::vec3& outHit ) const
    {
        const auto& camera = m_Scene->GetMainCamera().lock();
        if ( !camera )
            return false;

        const auto& tf = terrainEntity.GetComponent<ECS::TransformComponent>();

        auto [mouseX, mouseY] = GetMouseViewportSpace();
        const auto ray        = Common::Math::Ray::FromScreenPosition(
             { mouseX, mouseY }, camera->GetProjectionMatrix(), camera->GetViewMatrix(),
             camera->GetPosition(), static_cast<uint32_t>( m_ViewportData.Size.x ),
             static_cast<uint32_t>( m_ViewportData.Size.y ) );

        // Intersect with the horizontal plane at the terrain's base height. The splat map is XZ-indexed,
        // so the plane hit (ignoring displacement) is a good-enough position for v1.
        if ( std::abs( ray.Direction.y ) < 1e-5f )
            return false;
        const float t = ( tf.Translation.y - ray.Origin.y ) / ray.Direction.y;
        if ( t <= 0.0f )
            return false;
        outHit = ray.GetPoint( t );
        return true;
    }

    void ViewportPanel::PaintTerrainAtCursor( const ECS::Entity& terrainEntity )
    {
        auto& comp = terrainEntity.GetComponent<ECS::TerrainComponent>();
        auto& tf   = terrainEntity.GetComponent<ECS::TransformComponent>();

        const float    size = comp.Data.Size;
        const uint32_t res  = ECS::TerrainComponent::SplatResolution;
        if ( size <= 0.0f )
            return;
        if ( comp.SplatPixels.empty() )
            comp.SplatPixels.assign( static_cast<size_t>( res ) * res * 4, 0 );

        glm::vec3 hit;
        if ( !TerrainPickPoint( terrainEntity, hit ) )
            return;

        // Terrain-local UV (matches the shader's splat UV: (worldXZ - modelTranslationXZ)/Size + 0.5).
        const float u = ( hit.x - tf.Translation.x ) / size + 0.5f;
        const float v = ( hit.z - tf.Translation.z ) / size + 0.5f;
        if ( u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f )
            return;

        const int   cx       = static_cast<int>( u * res );
        const int   cy       = static_cast<int>( v * res );
        const float radiusPx = ( m_TerrainBrush.Radius / size ) * res;
        const int   channel  = std::clamp( m_TerrainBrush.Layer, 0, 2 ); // R=grass, G=rock, B=snow
        const int   r        = static_cast<int>( std::ceil( radiusPx ) );
        const float sign     = m_TerrainBrush.Erase ? -1.0f : 1.0f;

        for ( int dy = -r; dy <= r; ++dy )
        {
            for ( int dx = -r; dx <= r; ++dx )
            {
                const int px = cx + dx;
                const int py = cy + dy;
                if ( px < 0 || py < 0 || px >= (int)res || py >= (int)res )
                    continue;
                const float dist = std::sqrt( static_cast<float>( dx * dx + dy * dy ) );
                if ( dist > radiusPx )
                    continue;
                const float  falloff = 1.0f - dist / glm::max( radiusPx, 0.0001f );
                const size_t idx     = ( static_cast<size_t>( py ) * res + px ) * 4 + channel;
                const float  cur     = static_cast<float>( comp.SplatPixels[idx] );
                // Builds up over the frames the button is held (not instant).
                const float  delta   = sign * m_TerrainBrush.Strength * falloff * 60.0f;
                comp.SplatPixels[idx] =
                     static_cast<unsigned char>( glm::clamp( cur + delta, 0.0f, 255.0f ) );
            }
        }
        comp.SplatDirty = true;
    }

    void ViewportPanel::DrawBrushRing( const ECS::Entity& terrainEntity )
    {
        const auto& camera = m_Scene->GetMainCamera().lock();
        if ( !camera )
            return;

        glm::vec3 center;
        if ( !TerrainPickPoint( terrainEntity, center ) )
            return;

        const glm::mat4 mvp   = camera->GetProjectionMatrix() * camera->GetViewMatrix();
        const float     w     = m_ViewportData.Size.x;
        const float     h     = m_ViewportData.Size.y;
        const glm::vec2 vpPos = m_ViewportData.ViewportPos;

        const auto toScreen = [&]( const glm::vec3& world, ImVec2& out ) -> bool
        {
            const glm::vec4 clip = mvp * glm::vec4( world, 1.0f );
            if ( clip.w <= 1e-4f )
                return false;
            const glm::vec3 ndc = glm::vec3( clip ) / clip.w;
            out = ImVec2( vpPos.x + ( ndc.x * 0.5f + 0.5f ) * w,
                          vpPos.y + ( 1.0f - ( ndc.y * 0.5f + 0.5f ) ) * h );
            return true;
        };

        constexpr int N      = 48;
        const float   radius = m_TerrainBrush.Radius;
        ImVec2        pts[N];
        for ( int i = 0; i < N; ++i )
        {
            const float a = static_cast<float>( i ) / N * 2.0f * 3.14159265f;
            const glm::vec3 wp =
                 center + glm::vec3( std::cos( a ) * radius, 0.0f, std::sin( a ) * radius );
            if ( !toScreen( wp, pts[i] ) )
                return; // part of the ring is behind the camera — skip this frame
        }

        auto*         dl  = ImGui::GetWindowDrawList();
        const ImU32   col = IM_COL32( 255, 220, 60, 230 );
        for ( int i = 0; i < N; ++i )
            dl->AddLine( pts[i], pts[( i + 1 ) % N], col, 2.0f );

        ImVec2 centerScreen;
        if ( toScreen( center, centerScreen ) )
            dl->AddCircleFilled( centerScreen, 3.0f, col );
    }

    void ViewportPanel::UploadDirtySplatMaps()
    {
        auto& registry = m_Scene->GetRegistry();
        auto  view     = registry.view<ECS::TerrainComponent>();

        bool any = false;
        for ( auto e : view )
        {
            const auto& c = view.get<ECS::TerrainComponent>( e );
            if ( c.SplatDirty && !c.SplatPixels.empty() )
            {
                any = true;
                break;
            }
        }
        if ( !any )
            return;

        // Releasing/recreating a sampled image must not race in-flight GPU work.
        Graphic::Renderer::GetInstance().WaitDeviceIdle();

        const uint32_t res = ECS::TerrainComponent::SplatResolution;
        for ( auto e : view )
        {
            auto& c = view.get<ECS::TerrainComponent>( e );
            if ( !c.SplatDirty || c.SplatPixels.empty() )
                continue;

            if ( !c.SplatMap )
            {
                ::Desert::Core::Formats::Image2DSpecification spec = {
                     .Tag        = "TerrainSplatMap",
                     .Width      = res,
                     .Height     = res,
                     .Format     = ::Desert::Core::Formats::ImageFormat::RGBA8F,
                     .Mips       = 1,
                     .Data       = c.SplatPixels,
                     .Usage      = ::Desert::Core::Formats::Image2DUsage::Image2D,
                     .Properties = ::Desert::Core::Formats::ImageProperties::Sample,
                };
                c.SplatMap = Graphic::Image2D::Create( spec, nullptr );
            }
            else
            {
                c.SplatMap->GetImageSpecification().Data = c.SplatPixels;
                c.SplatMap->Invalidate();
            }
            c.SplatDirty = false;
        }
    }

} // namespace Desert::Editor