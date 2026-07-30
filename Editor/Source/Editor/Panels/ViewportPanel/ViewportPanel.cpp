#include "ViewportPanel.hpp"
#include <Editor/Core/DragPayloads.hpp>
#include <Editor/Core/EditorPreferences.hpp>

#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Editor/Core/Selection/SkeletonEditMode.hpp>
#include <Editor/Core/Commands/SceneCommands.hpp>
#include <Editor/Core/Selection/ViewportMode.hpp>
#include <Editor/Core/Selection/FoliagePaint.hpp>
#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Core/ThemeManager.hpp>
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
#include <Engine/Runtime/SelectionContext.hpp>
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
#include <cstdio>

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

        // Progress overlay while background cooks are in flight (top-left of the viewport). Padded card:
        // roomy inner margins, a spinner-style title row, and a labelled bar so it reads as a polished toast.
        if ( m_AsyncLoader->IsBusy() )
        {
            ImGui::SetNextWindowBgAlpha( 0.90f );
            ImGui::SetNextWindowPos( ImVec2( m_ViewportData.ViewportPos.x + 16.0f,
                                             m_ViewportData.ViewportPos.y + 16.0f ) );
            ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 18.0f, 14.0f ) );
            ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 8.0f );
            ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 1.0f );
            ImGui::Begin( "##AsyncLoadOverlay", nullptr,
                          ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                              ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing );

            const int   done  = m_AsyncLoader->Done2();
            const int   total = m_AsyncLoader->Total();
            const float frac  = m_AsyncLoader->Progress();

            ImGui::TextColored( ImVec4( 0.55f, 0.78f, 1.0f, 1.0f ), ICON_MDI_PROGRESS_DOWNLOAD );
            ImGui::SameLine( 0.0f, 8.0f );
            ImGui::TextUnformatted( "Loading meshes" );

            ImGui::Dummy( ImVec2( 0.0f, 6.0f ) );

            char overlayText[32];
            std::snprintf( overlayText, sizeof( overlayText ), "%d / %d", done, total );
            ImGui::ProgressBar( frac, ImVec2( 240.0f, 14.0f ), overlayText );

            ImGui::End();
            ImGui::PopStyleVar( 3 );
        }
    }

    void ViewportPanel::DrawViewportToolbar()
    {
        // Godot-style strip directly ABOVE the image. Left cluster = how you EDIT (mode, transform
        // tools, snap, contextual skeleton toggle); right edge = the camera gear. Small icons —
        // the viewport pixels are the star, the tools are furniture.
        bool canEditSkeleton = false;
        if ( const auto& sel = Core::SelectionManager::GetSelected(); sel.has_value() )
            if ( auto ref = m_Scene->FindEntityByID( *sel ); ref )
                canEditSkeleton = ref->get().HasComponent<ECS::SkinnedMeshComponent>();

        // Skeleton edit only makes sense in Select mode on a skinned mesh.
        if ( !canEditSkeleton || Core::ViewportMode::Get() != Core::EditorMode::Select )
            Core::SkeletonEditMode::SetActive( false );

        // While Skeleton Edit is active, ask the engine to render the selected mesh in BIND pose so
        // bone-gizmo edits are visible (an auto-playing clip would otherwise override them).
        if ( Core::SkeletonEditMode::IsActive() )
            Runtime::SelectionContext::SetBindPosePreview( Core::SelectionManager::GetSelected() );
        else
            Runtime::SelectionContext::SetBindPosePreview( std::nullopt );

        ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 4.0f );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 4.0f, 4.0f ) );

        // Breathing room: the row must not sit flush against the panel's left wall.
        ImGui::SetCursorPosX( ImGui::GetCursorPosX() + 6.0f );

        // --- Mode dropdown ---
        const char* kModes[] = { ICON_MDI_CURSOR_DEFAULT "  Select", ICON_MDI_GRASS "  Foliage" };
        int         mode     = static_cast<int>( Core::ViewportMode::Get() );
        ImGui::SetNextItemWidth( 118.0f );
        // WindowPadding is captured when the combo POPUP begins — push it here so the dropdown's
        // items keep a margin from the popup border instead of touching it.
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 8.0f, 6.0f ) );
        if ( ImGui::Combo( "##ViewportMode", &mode, kModes, IM_ARRAYSIZE( kModes ) ) )
            Core::ViewportMode::Set( static_cast<Core::EditorMode>( mode ) );
        ImGui::PopStyleVar();

        ImGui::SameLine();
        ImGui::TextDisabled( "|" );
        ImGui::SameLine();

        // --- Transform-tool toggles (GizmoState is the single source of truth; hotkeys mirror it) ---
        const auto opButton = [&]( const char* icon, Core::GizmoState::Operation op, const char* tip )
        {
            const bool active = Core::GizmoState::Get() == op;
            if ( active )
                ImGui::PushStyleColor( ImGuiCol_Button, ThemeManager::GetSelectedColor() );
            if ( ImGui::Button( icon ) )
                Core::GizmoState::Set( op );
            if ( active )
                ImGui::PopStyleColor();
            if ( ImGui::IsItemHovered() )
                ImGui::SetTooltip( "%s", tip );
            ImGui::SameLine();
        };
        opButton( ICON_MDI_CURSOR_DEFAULT_OUTLINE, Core::GizmoState::Operation::None, "Select (Esc)" );
        opButton( ICON_MDI_AXIS_ARROW, Core::GizmoState::Operation::Translate, "Move (T)" );
        opButton( ICON_MDI_ROTATE_ORBIT, Core::GizmoState::Operation::Rotate, "Rotate (R)" );
        opButton( ICON_MDI_ARROW_EXPAND_ALL, Core::GizmoState::Operation::Scale, "Scale (C)" );

        ImGui::TextDisabled( "|" );
        ImGui::SameLine();

        // --- Snap: magnet toggle (persistent) + right-click popup with the increments.
        // Ctrl during a drag temporarily inverts the toggle.
        {
            const bool snapOn = Core::GizmoState::PersistentSnap();
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

        // --- Right edge: View Mode (UE-style) + editor camera gear ---
        // One dropdown driving the engine's existing debug visualizations behind a single control
        // (instead of scattered Scene Settings checkboxes). Each mode maps to the underlying flags;
        // the current selection is derived back from them so external edits stay in sync.
        {
            auto& s = m_Scene->GetSettings();
            enum ViewMode
            {
                VM_Lit,
                VM_Wireframe,
                VM_Normals,
                VM_Albedo,
                VM_Metallic,
                VM_Roughness,
                VM_AO,
                VM_LightComplexity,
                VM_Overdraw,
                VM_MaterialComplexity,
                VM_ShadowCascades,
                VM_Count
            };
            const char* kViewModes[] = { ICON_MDI_LIGHTBULB_ON "  Lit",
                                         ICON_MDI_VECTOR_TRIANGLE "  Wireframe",
                                         ICON_MDI_AXIS_ARROW "  Normals",
                                         ICON_MDI_PALETTE "  Albedo (Unlit)",
                                         ICON_MDI_CIRCLE_HALF_FULL "  Metallic",
                                         ICON_MDI_BLUR "  Roughness",
                                         ICON_MDI_WEATHER_NIGHT "  Ambient Occlusion",
                                         ICON_MDI_FIRE "  Light Complexity",
                                         ICON_MDI_LAYERS_TRIPLE "  Overdraw",
                                         ICON_MDI_TEXTURE "  Material Complexity",
                                         ICON_MDI_LAYERS "  Shadow Cascades" };

            // Derive the active mode from the current settings (last-wins order matches the enum).
            int vm = VM_Lit;
            if ( s.WireframeMode )
                vm = VM_Wireframe;
            else if ( s.ShadowDebug == ::Desert::Core::ShadowDebugMode::Cascades )
                vm = VM_ShadowCascades;
            else if ( s.DeferredDebug == ::Desert::Core::DeferredDebugMode::Albedo )
                vm = VM_Albedo;
            else if ( s.DeferredDebug == ::Desert::Core::DeferredDebugMode::Normal )
                vm = VM_Normals;
            else if ( s.DeferredDebug == ::Desert::Core::DeferredDebugMode::Metallic )
                vm = VM_Metallic;
            else if ( s.DeferredDebug == ::Desert::Core::DeferredDebugMode::Roughness )
                vm = VM_Roughness;
            else if ( s.DeferredDebug == ::Desert::Core::DeferredDebugMode::AO )
                vm = VM_AO;
            else if ( s.DeferredDebug == ::Desert::Core::DeferredDebugMode::LightComplexity )
                vm = VM_LightComplexity;
            else if ( s.DeferredDebug == ::Desert::Core::DeferredDebugMode::Overdraw )
                vm = VM_Overdraw;
            else if ( s.DeferredDebug == ::Desert::Core::DeferredDebugMode::MaterialComplexity )
                vm = VM_MaterialComplexity;
            else if ( s.ShowNormals )
                vm = VM_Normals;

            const float gearW = ImGui::GetFrameHeight();
            const float vmX   = ImGui::GetWindowContentRegionMax().x - gearW - 6.0f - 160.0f;

            // Debug "Show" flags (grid, bounding boxes, colliders, wireframe, LOD) — moved out of Scene
            // Settings so everything "what to show in the viewport" lives next to the View Mode dropdown.
            ImGui::SameLine( vmX - gearW - 8.0f );
            if ( ImGui::Button( ICON_MDI_EYE_OUTLINE "##DebugShowFlags" ) )
                ImGui::OpenPopup( "##DebugShowFlagsPopup" );
            if ( ImGui::IsItemHovered() )
                ImGui::SetTooltip( "Show flags (grid, bounding boxes, colliders, wireframe, LOD)" );
            // Roomy padding + item spacing so the toggles aren't cramped against the popup border.
            ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 12.0f, 10.0f ) );
            if ( ImGui::BeginPopup( "##DebugShowFlagsPopup" ) )
            {
                ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 8.0f, 8.0f ) );
                ImGui::TextUnformatted( "Show" );
                ImGui::Separator();
                ImGui::Checkbox( "Grid", &s.ShowGrid );
                ImGui::Checkbox( "Bounding Boxes", &s.ShowBoundingBoxes );
                ImGui::BeginDisabled( !s.ShowBoundingBoxes );
                ImGui::ColorEdit3( "BB Color", &s.BoundingBoxColor.x );
                ImGui::SliderFloat( "BB Width", &s.BoundingBoxLineWidth, 1.0f, 10.0f, "%.1f" );
                ImGui::EndDisabled();
                ImGui::Checkbox( "Colliders", &s.ShowColliders );
                ImGui::Checkbox( "Wireframe", &s.WireframeMode );
                ImGui::Checkbox( "Mesh LOD (auto)", &s.MeshLOD );
                ImGui::PopStyleVar();
                ImGui::EndPopup();
            }
            ImGui::PopStyleVar(); // WindowPadding (pushed unconditionally before BeginPopup)

            ImGui::SameLine( vmX );
            ImGui::SetNextItemWidth( 154.0f );
            ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 8.0f, 6.0f ) );
            if ( ImGui::Combo( "##ViewMode", &vm, kViewModes, IM_ARRAYSIZE( kViewModes ) ) )
            {
                // Reset every debug channel, then set the one this mode needs.
                s.WireframeMode = false;
                s.ShowNormals   = false;
                s.DeferredDebug = ::Desert::Core::DeferredDebugMode::Off;
                s.ShadowDebug   = ::Desert::Core::ShadowDebugMode::Off;
                switch ( vm )
                {
                    case VM_Wireframe:
                        s.WireframeMode = true;
                        break;
                    case VM_Normals:
                        s.ShowNormals   = true;
                        s.DeferredDebug = ::Desert::Core::DeferredDebugMode::Normal;
                        break;
                    case VM_Albedo:
                        s.DeferredDebug = ::Desert::Core::DeferredDebugMode::Albedo;
                        break;
                    case VM_Metallic:
                        s.DeferredDebug = ::Desert::Core::DeferredDebugMode::Metallic;
                        break;
                    case VM_Roughness:
                        s.DeferredDebug = ::Desert::Core::DeferredDebugMode::Roughness;
                        break;
                    case VM_AO:
                        s.DeferredDebug = ::Desert::Core::DeferredDebugMode::AO;
                        break;
                    case VM_LightComplexity:
                        s.DeferredDebug = ::Desert::Core::DeferredDebugMode::LightComplexity;
                        break;
                    case VM_Overdraw:
                        s.DeferredDebug = ::Desert::Core::DeferredDebugMode::Overdraw;
                        break;
                    case VM_MaterialComplexity:
                        s.DeferredDebug = ::Desert::Core::DeferredDebugMode::MaterialComplexity;
                        break;
                    case VM_ShadowCascades:
                        s.ShadowDebug = ::Desert::Core::ShadowDebugMode::Cascades;
                        break;
                    default:
                        break; // VM_Lit
                }
            }
            ImGui::PopStyleVar();
            if ( ImGui::IsItemHovered() )
                ImGui::SetTooltip( "Viewport view mode. Buffer views (Albedo/Metallic/Roughness/AO)\n"
                                   "need the Deferred render path." );
        }

        // --- Right edge: editor camera settings (speed) behind a gear button ---
        ImGui::SameLine( ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight() - 6.0f );
        if ( ImGui::Button( ICON_MDI_COG "##CamSettings" ) )
            ImGui::OpenPopup( "##EditorCameraSettings" );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "Editor camera settings" );
        // Roomy padding so the settings aren't cramped against the popup border.
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 14.0f, 12.0f ) );
        if ( ImGui::BeginPopup( "##EditorCameraSettings" ) )
        {
            ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 8.0f, 8.0f ) );
            ImGui::TextUnformatted( "Editor Camera" );
            ImGui::Separator();
            if ( auto cam = m_Scene->GetMainCamera().lock() )
            {
                if ( auto* editorCam = dynamic_cast<::Desert::Core::EditorCamera*>( cam.get() ) )
                {
                    constexpr float kW = 170.0f;

                    // Projection type: Perspective / Orthographic (view is unchanged; only the projection).
                    const char* kTypes[] = { "Perspective", "Orthographic" };
                    int         type     = static_cast<int>( editorCam->GetProjectionType() );
                    ImGui::SetNextItemWidth( kW );
                    if ( ImGui::Combo( "Type", &type, kTypes, IM_ARRAYSIZE( kTypes ) ) )
                        editorCam->SetProjectionType( static_cast<::Desert::Core::ProjectionType>( type ) );

                    if ( editorCam->GetProjectionType() == ::Desert::Core::ProjectionType::Perspective )
                    {
                        float fov = editorCam->GetFOV();
                        ImGui::SetNextItemWidth( kW );
                        if ( ImGui::SliderFloat( "FOV", &fov, 20.0f, 120.0f, "%.0f" ) )
                            editorCam->SetFOV( fov );
                    }
                    else
                    {
                        float size = editorCam->GetOrthoSize();
                        ImGui::SetNextItemWidth( kW );
                        if ( ImGui::SliderFloat( "Ortho Size", &size, 1.0f, 100.0f, "%.1f" ) )
                            editorCam->SetOrthoSize( size );
                    }

                    float nearP = editorCam->GetNear();
                    ImGui::SetNextItemWidth( kW );
                    if ( ImGui::SliderFloat( "Near", &nearP, 0.001f, 10.0f, "%.3f" ) )
                        editorCam->SetNear( nearP );

                    float farP = editorCam->GetFar();
                    ImGui::SetNextItemWidth( kW );
                    if ( ImGui::SliderFloat( "Far", &farP, 100.0f, 5000.0f, "%.0f" ) )
                        editorCam->SetFar( farP );

                    float spd = editorCam->GetMovementSpeed();
                    ImGui::SetNextItemWidth( kW );
                    if ( ImGui::SliderFloat( "Speed", &spd, 0.1f, 10.0f, "%.2fx" ) )
                        editorCam->SetMovementSpeed( spd );
                }
                else
                {
                    ImGui::TextDisabled( "(only in editor view, not Play)" );
                }
            }
            ImGui::PopStyleVar();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(); // camera-settings WindowPadding

        ImGui::PopStyleVar( 2 );
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

        // Godot-style: one compact toolbar ROW above the image (mode, transform tools, snap,
        // contextual skeleton toggle, camera gear on the right). Nothing floats over the scene
        // pixels anymore, so the picture is clean and picking never fights an overlay window.
        DrawViewportToolbar();

        // The viewport rect is whatever remains BELOW the toolbar row.
        const ImVec2 mousePos   = ::ImGui::GetMousePos();
        const ImVec2 imagePos   = ImGui::GetCursorScreenPos();
        ImVec2       imageAvail = ImGui::GetContentRegionAvail();
        imageAvail.x            = std::max( imageAvail.x, 1.0f );
        imageAvail.y            = std::max( imageAvail.y, 1.0f );

        m_ViewportData.ViewportPos   = { imagePos.x, imagePos.y };
        m_ViewportData.MousePosition = glm::vec2( mousePos.x - imagePos.x, mousePos.y - imagePos.y );
        const auto oldSize           = m_ViewportData.Size;

        m_ViewportData.Size      = { imageAvail.x, imageAvail.y };
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

            // Drag a material (.demat) onto the viewport: mouse-pick the mesh under the cursor and
            // assign the material to its elements (UE-style drop-on-object).
            if ( const ImGuiPayload* payload =
                      ImGui::AcceptDragDropPayload( ::Desert::Editor::DragPayloads::MaterialAsset );
                 payload && m_AssetManager )
            {
                const std::string path( static_cast<const char*>( payload->Data ),
                                        payload->DataSize > 0 ? payload->DataSize - 1 : 0 );
                AssignMaterialAtCursor( path );
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

        // Corner XYZ orientation triad — always shown so the world axes are readable at a glance.
        DrawViewAxisGizmo( m_ViewportData.ViewportPos, m_ViewportData.Size );

        // Perf HUD (View -> Perf HUD): FPS + frame graph + top CPU scopes, useful in Play too.
        if ( EditorPreferences::Get().ShowPerfHud )
            m_PerfHud.Draw( ImVec2( m_ViewportData.ViewportPos.x, m_ViewportData.ViewportPos.y ),
                            ImVec2( m_ViewportData.ViewportPos.x + m_ViewportData.Size.x,
                                    m_ViewportData.ViewportPos.y + m_ViewportData.Size.y ) );
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

    void ViewportPanel::DrawViewAxisGizmo( const glm::vec2& viewportPos, const glm::vec2& viewportSize )
    {
        const auto camera = m_Scene->GetMainCamera().lock();
        if ( !camera )
            return;

        // Rotation-only part of the view matrix maps world directions into view space (x=right, y=up,
        // z=toward-viewer). We only need orientation, so drop translation.
        const glm::mat3 viewRot = glm::mat3( camera->GetViewMatrix() );

        const float  radius = 34.0f;
        const ImVec2 center( viewportPos.x + viewportSize.x - radius - 18.0f, viewportPos.y + radius + 18.0f );
        ImDrawList*  dl = ::ImGui::GetWindowDrawList();

        struct Axis
        {
            glm::vec3   Dir;
            ImU32       Color;
            const char* Label;
        };
        const Axis axes[3] = {
            { { 1.0f, 0.0f, 0.0f }, IM_COL32( 232, 88, 88, 255 ), "X" },
            { { 0.0f, 1.0f, 0.0f }, IM_COL32( 120, 208, 96, 255 ), "Y" },
            { { 0.0f, 0.0f, 1.0f }, IM_COL32( 92, 152, 240, 255 ), "Z" },
        };

        // One drawable tip per axis END (+ and -). Sort back-to-front by view-space depth so the nearer
        // axis ends overpaint the farther ones (a readable 3D triad instead of flat crossing lines).
        struct Tip
        {
            ImVec2      Pos;
            float       Depth;
            ImU32       Color;
            bool        Positive;
            const char* Label;
            glm::vec3   WorldDir; // the world-space axis end this tip represents (a.Dir * s)
        };
        std::array<Tip, 6> tips2;
        int                n = 0;
        for ( const Axis& a : axes )
            for ( float s : { 1.0f, -1.0f } )
            {
                const glm::vec3 v = viewRot * ( a.Dir * s );
                tips2[n++] = Tip{ ImVec2( center.x + v.x * radius, center.y - v.y * radius ),
                                  v.z,
                                  a.Color,
                                  s > 0.0f,
                                  a.Label,
                                  a.Dir * s };
            }
        std::sort( tips2.begin(), tips2.end(), []( const Tip& l, const Tip& r ) { return l.Depth < r.Depth; } );

        // Clickable: a tip under the cursor snaps the editor camera to view FROM that axis end (forward =
        // -worldDir). Hover state suppresses picking (see OnMousePressed). Nearest-to-cursor tip wins.
        const ImVec2 mouse = ::ImGui::GetMousePos();
        auto*        editorCam =
             m_Scene ? dynamic_cast<::Desert::Core::EditorCamera*>( camera.get() ) : nullptr;
        const float  dxg = mouse.x - center.x, dyg = mouse.y - center.y;
        m_ViewAxisGizmoHovered = editorCam && ( dxg * dxg + dyg * dyg ) <= ( radius + 8.0f ) * ( radius + 8.0f );

        int   hotTip  = -1;
        float hotDist = 11.0f; // click/hover radius around a tip in pixels
        for ( int i = 0; i < n; ++i )
        {
            const float dx = mouse.x - tips2[i].Pos.x, dy = mouse.y - tips2[i].Pos.y;
            const float d  = std::sqrt( dx * dx + dy * dy );
            if ( d < hotDist )
            {
                hotDist = d;
                hotTip  = i;
            }
        }

        dl->AddCircleFilled( center, radius + 8.0f, IM_COL32( 20, 20, 24, 130 ) );
        for ( int i = 0; i < n; ++i )
        {
            const Tip& t   = tips2[i];
            const bool hot = ( i == hotTip ) && editorCam;
            if ( t.Positive )
            {
                dl->AddLine( center, t.Pos, t.Color, 2.0f );
                dl->AddCircleFilled( t.Pos, hot ? 10.0f : 8.0f, t.Color );
                if ( hot )
                    dl->AddCircle( t.Pos, 10.0f, IM_COL32( 255, 255, 255, 220 ), 0, 2.0f );
                const ImVec2 ts = ::ImGui::CalcTextSize( t.Label );
                dl->AddText( ImVec2( t.Pos.x - ts.x * 0.5f, t.Pos.y - ts.y * 0.5f ), IM_COL32( 15, 15, 18, 255 ),
                             t.Label );
            }
            else
            {
                // Negative ends: hollow dot, no label — reads as "the back of the axis".
                dl->AddCircleFilled( t.Pos, hot ? 8.0f : 6.0f, IM_COL32( 40, 40, 46, 200 ) );
                dl->AddCircle( t.Pos, hot ? 8.0f : 6.0f, hot ? IM_COL32( 255, 255, 255, 220 ) : t.Color, 0,
                               1.6f );
            }
        }

        if ( hotTip >= 0 && editorCam && ::ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
            editorCam->SnapToDirection( -tips2[hotTip].WorldDir );
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
        // A click on the corner view-axis gizmo snaps the camera (handled in DrawViewAxisGizmo) — don't also
        // pick the object behind it.
        if ( m_ViewAxisGizmoHovered )
            return false;

        // The pick must fire ONLY over the rendered scene image — not the toolbar/overlay widgets that sit
        // in the same viewport window. IsHovered (IsWindowHovered) is true for the whole window, so a click
        // on e.g. the "Skeleton" toolbar button used to leak here, Raycast-miss, and clear the selection.
        const ImVec2 mp        = ::ImGui::GetMousePos();
        const auto&  vp        = m_ViewportData;
        const bool   overImage = mp.x >= vp.ViewportPos.x && mp.y >= vp.ViewportPos.y &&
                                 mp.x < vp.ViewportPos.x + vp.Size.x && mp.y < vp.ViewportPos.y + vp.Size.y;

        if ( e.GetMouseButton() == Common::MouseButton::Left && !m_TerrainTool.BrushEnabled() &&
             Core::ViewportMode::Get() == Core::EditorMode::Select && m_ViewportData.IsHovered && overImage )
        {
            // Skeleton Edit mode: LMB selects the nearest bone joint under the cursor (keeping the skinned
            // mesh selected) rather than picking a new entity — unless the bone gizmo is being interacted with.
            if ( Core::SkeletonEditMode::IsActive() && !m_Gizmo.IsHovered() )
            {
                const int bone = m_LightGizmoRenderer->PickBone( ::ImGui::GetMousePos() );
                if ( bone >= 0 )
                    Core::SkeletonEditMode::SetSelectedBone( bone );
            }
            else
            {
                m_Picking.Pick( *m_Scene, m_ViewportData.MousePosition, m_ViewportData.Size,
                                m_Gizmo.IsHovered() || m_LightGizmoRenderer->IsLightIconHovered(),
                                ::ImGui::GetIO().KeyCtrl );
            }
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
            case Common::KeyCode::F:
                // Frame the selected entity (Unity/Godot 'F').
                if ( const auto sel = Core::SelectionManager::GetSelected() )
                    if ( auto cam = m_Scene->GetMainCamera().lock() )
                        if ( auto* editorCam = dynamic_cast<::Desert::Core::EditorCamera*>( cam.get() ) )
                            if ( auto ref = m_Scene->FindEntityByID( *sel ) )
                                editorCam->Focus( glm::vec3( ref->get().GetWorldTransform()[3] ) );
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

    void ViewportPanel::AssignMaterialAtCursor( const std::string& materialPath )
    {
        const auto& mainCamera = m_Scene->GetMainCamera().lock();
        if ( !mainCamera || !m_AssetManager )
            return;

        // Same screen->world ray the click-picker uses (mouse position is already viewport-local).
        const auto ray = Common::Math::Ray::FromScreenPosition(
             { m_ViewportData.MousePosition.x, m_ViewportData.MousePosition.y },
             mainCamera->GetProjectionMatrix(), mainCamera->GetViewMatrix(), mainCamera->GetPosition(),
             static_cast<uint32_t>( m_ViewportData.Size.x ), static_cast<uint32_t>( m_ViewportData.Size.y ) );

        ::Desert::Core::RaycastHit hit;
        if ( !m_Scene->Raycast( ray, hit ) )
            return;
        auto ref = m_Scene->FindEntityByID( hit.Entity );
        if ( !ref || !ref->get().HasComponent<ECS::StaticMeshComponent>() )
            return;
        const ECS::Entity& entity = ref->get();

        // Resolve/register the dropped material (same path the slot editor's drop target takes).
        auto& mgr   = const_cast<Assets::AssetManager&>( *m_AssetManager );
        auto  asset = mgr.FindByPath<Assets::SurfaceMaterialAsset>( materialPath );
        if ( !asset )
            asset = mgr.CreateAsset<Assets::SurfaceMaterialAsset>( Assets::AssetPriority::High, materialPath );
        if ( !asset )
            return;
        const auto handle = asset->GetMetadata().Handle;
        if ( !Runtime::ResourceRegistry::GetMaterialService()->Get( handle ) )
            Runtime::ResourceRegistry::GetMaterialService()->RegisterAsset( asset );

        // Assign every element (the renderer repeats the last slot anyway) and select the entity so
        // the Materials panel shows the result of the drop immediately.
        auto& smc = m_Scene->GetRegistry().get<ECS::StaticMeshComponent>( entity.GetHandle() );
        const size_t count = std::max<size_t>( size_t{ 1 }, smc.MaterialSlots.size() );
        smc.MaterialSlots.assign( count, handle );
        smc.RuntimeMaterialInstances.clear();
        Core::SelectionManager::SetSelected( hit.Entity );
    }

} // namespace Desert::Editor
