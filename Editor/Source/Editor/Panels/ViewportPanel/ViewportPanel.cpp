#include "ViewportPanel.hpp"
#include <Editor/Core/DragPayloads.hpp>
#include <Editor/Core/EditorPreferences.hpp>

#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Editor/Core/Selection/SkeletonEditMode.hpp>
#include <Editor/Core/Commands/SceneCommands.hpp>
#include <Editor/Core/Selection/ViewportMode.hpp>
#include <Editor/Core/Selection/FoliagePaint.hpp>
#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Panels/MeshEditor/MeshEditorPanel.hpp>
#include <Editor/Import/MeshDnD.hpp>
#include <Editor/Import/MeshMaterial.hpp>
#include <Editor/Import/AsyncMeshLoader.hpp>
#include <filesystem>
#include <Engine/Geometry/DynamicMesh.hpp>
#include <Engine/Geometry/PrimitiveMeshFactory.hpp>
#include <Engine/Geometry/SkinnedMesh.hpp>
#include <Engine/Animation/Skeleton.hpp>
#include <functional>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Prefab/PrefabAsset.hpp>
#include <Engine/Assets/Mesh/SurfaceMaterialAsset.hpp>
#include <Engine/Assets/Mesh/MeshAsset.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Core/Formats/ImageFormat.hpp>
#include <Common/Core/Math/Ray.hpp>

#include <ImGuizmo.h>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <limits>
#include <random>

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
        m_AsyncLoader        = std::make_unique<AsyncMeshLoader>(); // starts the background cook worker
    }

    // Out-of-line so unique_ptr<AsyncMeshLoader> destroys with the complete type (joins the worker thread).
    ViewportPanel::~ViewportPanel() = default;

    void ViewportPanel::UpdateAsyncLoads()
    {
        if ( !m_AsyncLoader || !m_AssetManager )
            return;

        auto& mgr = const_cast<Assets::AssetManager&>( *m_AssetManager );
        for ( const auto& done : m_AsyncLoader->PollCompleted() )
        {
            // The cook finished on the worker -> the main-thread register is now fast (already cooked). Spawn
            // the matching component: a rigged source becomes a SkinnedMesh (+ Animation) so a character can be
            // animated; everything else stays a StaticMesh + gets the pack's sidecar material.
            const auto resolved = MeshDnD::ResolveOrImportMesh( mgr, done.SourcePath );
            if ( resolved.Handle.IsNull() )
                continue;
            if ( auto ref = m_Scene->FindEntityByID( Common::UUID( done.UserData ) ); ref )
            {
                ECS::Entity e = ref->get(); // Entity is a lightweight value handle -> copy to operate mutably
                if ( resolved.Skinned )
                {
                    // Swap the pending StaticMeshComponent for a skinned one + an Animator, so the rig renders
                    // and its clips can be picked in Details immediately.
                    if ( e.HasComponent<ECS::StaticMeshComponent>() )
                        e.RemoveComponent<ECS::StaticMeshComponent>();
                    if ( !e.HasComponent<ECS::SkinnedMeshComponent>() )
                        e.AddComponent<ECS::SkinnedMeshComponent>();
                    e.GetComponent<ECS::SkinnedMeshComponent>().MeshHandle = resolved.Handle;
                    if ( !e.HasComponent<ECS::AnimationComponent>() )
                        e.AddComponent<ECS::AnimationComponent>();
                }
                else
                {
                    if ( e.HasComponent<ECS::StaticMeshComponent>() )
                        e.GetComponent<ECS::StaticMeshComponent>().MeshHandle = resolved.Handle;
                    ApplySidecarMaterial( e, done.SourcePath );
                }
            }
        }

        // Progress overlay while background cooks are in flight (top-left of the viewport).
        if ( m_AsyncLoader->IsBusy() )
        {
            ImGui::SetNextWindowBgAlpha( 0.85f );
            ImGui::SetNextWindowPos( ImVec2( m_ViewportData.ViewportPos.x + 14.0f,
                                             m_ViewportData.ViewportPos.y + 14.0f ) );
            ImGui::Begin( "##AsyncLoadOverlay", nullptr,
                          ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                              ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing );
            ImGui::Text( "Loading meshes  %d / %d", m_AsyncLoader->Done2(), m_AsyncLoader->Total() );
            ImGui::ProgressBar( m_AsyncLoader->Progress(), ImVec2( 220.0f, 0.0f ) );
            ImGui::End();
        }
    }

    void ViewportPanel::OnUIRender()
    {
        UpdateAsyncLoads(); // spawn any meshes whose background cook just finished (+ progress bar)

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

        // Feed the fly-camera its input gate: WASD/QE/arrows work while the viewport is hovered
        // and no text field owns the keyboard (otherwise typing "wasd" in a search box flies away).
        if ( auto* editorCam = dynamic_cast<::Desert::Core::EditorCamera*>( mainCamera.get() ) )
        {
            editorCam->SetInputEnabled( m_ViewportData.IsHovered && !ImGui::GetIO().WantTextInput );
        }

        // Render scene
        m_UIHelper->Image( m_Scene->GetFinalImage(), { m_ViewportData.Size.x, m_ViewportData.Size.y } );

        // UE5-style viewport toolbar (floating overlay, top-left): a MODE dropdown (Select / Foliage), the
        // contextual Skeleton-Edit toggle (Select mode + skinned mesh only), and a gear popup for the editor
        // camera settings (speed). Replaces the old single "Object Mode" button + always-on speed slider.
        {
            bool canEditSkeleton = false;
            if ( const auto& sel = Core::SelectionManager::GetSelected(); sel.has_value() )
                if ( auto ref = m_Scene->FindEntityByID( *sel ); ref )
                    canEditSkeleton = ref->get().HasComponent<ECS::SkinnedMeshComponent>();

            // Skeleton edit only makes sense in Select mode on a skinned mesh.
            if ( !canEditSkeleton || Core::ViewportMode::Get() != Core::EditorMode::Select )
                Core::SkeletonEditMode::SetActive( false );

            ImGui::SetNextWindowPos( ImVec2( m_ViewportData.ViewportPos.x + 12.0f,
                                             m_ViewportData.ViewportPos.y + 12.0f ) );
            const ImGuiWindowFlags overlayFlags =
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

            // Polished, rounded, padded toolbar (a flat semi-auto-resized box read as "crude" before).
            ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 6.0f );
            ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 5.0f );
            ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 7.0f, 5.0f ) );
            ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 6.0f, 4.0f ) );
            ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( 0.10f, 0.10f, 0.12f, 0.88f ) );
            ImGui::PushStyleColor( ImGuiCol_FrameBg, ImVec4( 0.18f, 0.18f, 0.21f, 1.0f ) );
            ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.18f, 0.18f, 0.21f, 1.0f ) );

            if ( ImGui::Begin( "##ViewportModeOverlay", nullptr, overlayFlags ) )
            {
                // --- Mode dropdown ---
                const char* kModes[] = { ICON_MDI_CURSOR_DEFAULT "  Select", ICON_MDI_GRASS "  Foliage" };
                int         mode     = static_cast<int>( Core::ViewportMode::Get() );
                ImGui::SetNextItemWidth( 128.0f );
                if ( ImGui::Combo( "##ViewportMode", &mode, kModes, IM_ARRAYSIZE( kModes ) ) )
                    Core::ViewportMode::Set( static_cast<Core::EditorMode>( mode ) );

                // --- Select mode: contextual Skeleton-Edit toggle (skinned mesh only) ---
                if ( Core::ViewportMode::Get() == Core::EditorMode::Select && canEditSkeleton )
                {
                    const bool active = Core::SkeletonEditMode::IsActive();
                    ImGui::SameLine();
                    if ( active )
                        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.85f, 0.45f, 0.1f, 1.0f ) );
                    if ( ImGui::Button( ICON_MDI_BONE "  Skeleton" ) )
                        Core::SkeletonEditMode::Toggle();
                    if ( active )
                        ImGui::PopStyleColor();
                    if ( active )
                    {
                        ImGui::SameLine();
                        bool showNames = Core::SkeletonEditMode::ShowAllNames();
                        if ( ImGui::Checkbox( "Names", &showNames ) )
                            Core::SkeletonEditMode::SetShowAllNames( showNames );
                    }
                }

                // --- Snap: magnet toggle (persistent) + right-click/arrow popup with the increments.
                // Ctrl during a drag temporarily inverts the toggle.
                {
                    const bool snapOn = Core::GizmoState::PersistentSnap();
                    ImGui::SameLine();
                    if ( snapOn )
                        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.20f, 0.55f, 0.95f, 1.0f ) );
                    if ( ImGui::Button( ICON_MDI_MAGNET "##SnapToggle" ) )
                        Core::GizmoState::SetPersistentSnap( !snapOn );
                    if ( snapOn )
                        ImGui::PopStyleColor();
                    if ( ImGui::IsItemHovered() )
                        ImGui::SetTooltip( "Snap %s (Ctrl inverts while dragging).\nRight-click: snap steps",
                                           snapOn ? "ON" : "OFF" );
                    if ( ImGui::IsItemClicked( ImGuiMouseButton_Right ) )
                        ImGui::OpenPopup( "##SnapSettings" );
                    if ( ImGui::BeginPopup( "##SnapSettings" ) )
                    {
                        ImGui::TextUnformatted( "Snap steps" );
                        ImGui::Separator();
                        float t = Core::GizmoState::TranslateSnap();
                        float r = Core::GizmoState::RotateSnapDegrees();
                        float s = Core::GizmoState::ScaleSnap();
                        ImGui::SetNextItemWidth( 130.0f );
                        if ( ImGui::DragFloat( "Move (m)", &t, 0.05f, 0.01f, 100.0f, "%.2f" ) )
                            Core::GizmoState::SetTranslateSnap( t );
                        ImGui::SetNextItemWidth( 130.0f );
                        if ( ImGui::DragFloat( "Rotate (deg)", &r, 0.5f, 0.1f, 180.0f, "%.1f" ) )
                            Core::GizmoState::SetRotateSnapDegrees( r );
                        ImGui::SetNextItemWidth( 130.0f );
                        if ( ImGui::DragFloat( "Scale", &s, 0.01f, 0.01f, 10.0f, "%.2f" ) )
                            Core::GizmoState::SetScaleSnap( s );
                        ImGui::EndPopup();
                    }
                }

                // --- Editor camera settings (speed) behind a gear button ---
                ImGui::SameLine();
                if ( ImGui::Button( ICON_MDI_COG "##CamSettings" ) )
                    ImGui::OpenPopup( "##EditorCameraSettings" );
                if ( ImGui::IsItemHovered() )
                    ImGui::SetTooltip( "Editor camera settings" );
                if ( ImGui::BeginPopup( "##EditorCameraSettings" ) )
                {
                    ImGui::TextUnformatted( "Editor Camera" );
                    ImGui::Separator();
                    if ( auto cam = m_Scene->GetMainCamera().lock() )
                    {
                        if ( auto* editorCam = dynamic_cast<::Desert::Core::EditorCamera*>( cam.get() ) )
                        {
                            float spd = editorCam->GetMovementSpeed();
                            ImGui::SetNextItemWidth( 160.0f );
                            if ( ImGui::SliderFloat( "Speed", &spd, 0.1f, 10.0f, "%.2fx" ) )
                                editorCam->SetMovementSpeed( spd );
                        }
                        else
                        {
                            ImGui::TextDisabled( "(only in editor view, not Play)" );
                        }
                    }
                    ImGui::EndPopup();
                }
            }
            ImGui::End();
            ImGui::PopStyleColor( 3 );
            ImGui::PopStyleVar( 4 );
        }

        // Drag a prefab file from the File Explorer onto the viewport to instantiate it into the scene.
        if ( ImGui::BeginDragDropTarget() )
        {
            if ( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( ::Desert::Editor::DragPayloads::PrefabFile ); payload && m_AssetManager )
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
                    auto root = prefab->Instantiate( m_Scene.get(), *m_AssetManager, nullptr );
                    if ( root )
                        Commands::NotifyCreated( { root.GetComponent<ECS::UUIDComponent>().UUID } );
                }
            }

            // Drag a mesh source (.obj/.fbx/.gltf/...) from the File Explorer onto the viewport to spawn it
            // as a new entity. Cooks the source on demand (see MeshDnD).
            if ( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( ::Desert::Editor::DragPayloads::MeshAsset ); payload && m_AssetManager )
            {
                const std::string path( static_cast<const char*>( payload->Data ),
                                        payload->DataSize > 0 ? payload->DataSize - 1 : 0 );

                // ASYNC spawn: create the (empty) entity NOW and cook the mesh on a worker thread so a heavy
                // FBX doesn't hitch the editor. UpdateAsyncLoads() assigns the mesh once the cook finishes.
                const std::string name = std::filesystem::path( path ).stem().string();
                auto&             e    = m_Scene->CreateNewEntity( std::string( name ) );
                e.AddComponent<ECS::StaticMeshComponent>(); // pending: no MeshHandle until the cook completes
                const auto uuid = e.GetComponent<ECS::UUIDComponent>().UUID;
                Core::SelectionManager::SetSelected( uuid );
                Commands::NotifyCreated( { uuid } ); // undo removes the pending entity; the async cook
                                                     // no-ops when its target entity is gone
                m_AsyncLoader->Request( path, static_cast<uint64_t>( uuid ) );
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
            m_TerrainTool.DrawOverlay( *terrainEntity );

        const bool painting = terrainEntity && m_TerrainTool.BrushEnabled();

        const bool foliageMode = Core::ViewportMode::Get() == Core::EditorMode::Foliage;

        // Handle gizmos (Select mode only — Foliage mode uses LMB to paint, not to gizmo/pick).
        m_Gizmo.ResetHovered();
        if ( !foliageMode )
        {
            if ( Core::SkeletonEditMode::IsActive() )
            {
                // skeleton edit owns the gizmo (edits the selected bone, not the object)
                m_Gizmo.RenderBone( *m_Scene, m_ViewportData.ViewportPos, m_ViewportData.Size );
            }
            else if ( m_Gizmo.IsActive() && !painting )
            {
                m_Gizmo.RenderObject( *m_Scene, m_ViewportData.ViewportPos, m_ViewportData.Size );
            }
        }

        if ( painting && m_ViewportData.IsHovered )
        {
            // Replace the OS pointer with the brush: hide the arrow and draw the world-space radius ring.
            ImGui::SetMouseCursor( ImGuiMouseCursor_None );
            if ( const auto& camera = m_Scene->GetMainCamera().lock() )
            {
                auto [mx, my]  = GetMouseViewportSpace();
                const auto ray = Common::Math::Ray::FromScreenPosition(
                     { mx, my }, camera->GetProjectionMatrix(), camera->GetViewMatrix(),
                     camera->GetPosition(), static_cast<uint32_t>( m_ViewportData.Size.x ),
                     static_cast<uint32_t>( m_ViewportData.Size.y ) );
                const glm::mat4 viewProj = camera->GetProjectionMatrix() * camera->GetViewMatrix();
                m_TerrainTool.DrawRing( ray, *terrainEntity, viewProj, m_ViewportData.Size,
                                        m_ViewportData.ViewportPos );

                if ( ImGui::IsMouseDown( ImGuiMouseButton_Left ) &&
                     !ImGui::IsAnyItemActive() ) // don't paint while dragging the brush sliders
                    m_TerrainTool.Paint( ray, *terrainEntity );
            }
        }

        // --- Foliage paint mode: type panel + LMB scatter/erase (FoliagePaintTool) ---
        if ( foliageMode )
        {
            m_FoliageTool.DrawPanel( *m_Scene, m_AssetManager, m_ViewportData.ViewportPos );
            if ( m_ViewportData.IsHovered && Core::FoliagePaint::HasActive() &&
                 ImGui::IsMouseDown( ImGuiMouseButton_Left ) && !ImGui::IsAnyItemActive() )
            {
                if ( const auto& camera = m_Scene->GetMainCamera().lock() )
                {
                    auto [mx, my]  = GetMouseViewportSpace();
                    const auto ray = Common::Math::Ray::FromScreenPosition(
                         { mx, my }, camera->GetProjectionMatrix(), camera->GetViewMatrix(),
                         camera->GetPosition(), static_cast<uint32_t>( m_ViewportData.Size.x ),
                         static_cast<uint32_t>( m_ViewportData.Size.y ) );
                    m_FoliageTool.Paint( *m_Scene, ray );
                }
            }
        }

        // Editor gizmos (light/camera icons + frustums) are authoring aids — hide them in Play/Paused so the
        // running game view is clean.
        if ( m_Scene->GetState() == ::Desert::Core::Scene::SceneState::Edit )
            m_LightGizmoRenderer->Render( m_ViewportData.Size.x, m_ViewportData.Size.y,
                                          m_ViewportData.ViewportPos.x, m_ViewportData.ViewportPos.y );

        // Perf HUD (View -> Perf HUD): FPS + frame graph + top CPU scopes, useful in Play too.
        if ( EditorPreferences::Get().ShowPerfHud )
            m_PerfHud.Draw( viewportMin, viewportMax );
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
        m_TerrainTool.UploadDirtySplatMaps( *m_Scene );
    }

    std::pair<float, float> ViewportPanel::GetMouseViewportSpace() const
    {
        return { m_ViewportData.MousePosition.x, m_ViewportData.MousePosition.y };
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
        // LMB picks/selects ONLY in Select mode and when no brush is active (terrain brush / Foliage paint
        // both consume LMB in OnUIRender instead).
        if ( e.GetMouseButton() == Common::MouseButton::Left && !m_TerrainTool.BrushEnabled() &&
             Core::ViewportMode::Get() == Core::EditorMode::Select && m_ViewportData.IsHovered )
        {
            m_Picking.Pick( *m_Scene, m_ViewportData.MousePosition, m_ViewportData.Size, m_Gizmo.IsHovered(),
                            ::ImGui::GetIO().KeyCtrl );
        }

        return false;
    }

    bool ViewportPanel::OnKeyPressedEvent( Common::KeyPressedEvent& e )
    {
        switch ( e.GetKeyCode() )
        {
            case Common::KeyCode::Escape:
                // First Esc turns the gizmo off; a second Esc (gizmo already off) clears the selection.
                if ( m_Gizmo.GetOperation() == Tools::GizmoController::Operation::None )
                    Core::SelectionManager::ClearSelection();
                else
                    m_Gizmo.SetOperation( Tools::GizmoController::Operation::None );
                break;
            case Common::KeyCode::T:
                m_Gizmo.SetOperation( Tools::GizmoController::Operation::Translate );
                break;
            case Common::KeyCode::R:
                m_Gizmo.SetOperation( Tools::GizmoController::Operation::Rotate );
                break;
            case Common::KeyCode::C:
                m_Gizmo.SetOperation( Tools::GizmoController::Operation::Scale );
                break;
        }
        return false;
    }

    void ViewportPanel::ApplySidecarMaterial( ECS::Entity& entity, const std::string& meshSourcePath )
    {
        if ( !m_AssetManager )
            return;

        const auto h = MeshMaterial::ResolveSidecar( const_cast<Assets::AssetManager&>( *m_AssetManager ),
                                                     meshSourcePath );
        if ( h.IsNull() )
            return;

        // Assign to every material slot (one per submesh).
        auto&  smc   = entity.GetComponent<ECS::StaticMeshComponent>();
        size_t count = 1;
        if ( auto* meshAsset = Runtime::ResourceRegistry::GetMeshService()->GetAsset( smc.MeshHandle ) )
            if ( const auto n = meshAsset->GetMaterialHandles().size(); n > 0 )
                count = n;
        smc.MaterialSlots.assign( count, h );
        smc.RuntimeMaterialInstances.clear();
    }




} // namespace Desert::Editor
