#include "ViewportPanel.hpp"
#include <Editor/Core/DragPayloads.hpp>
#include <Editor/Core/EditorPreferences.hpp>

#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Editor/Core/Selection/UIPreview.hpp>
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
#include <Engine/UI/UICanvasRenderer.hpp>
#include <Engine/UI/UILayout.hpp>
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

    namespace
    {
        // In-scene UI authoring — same structure the UI Editor panel builds (a UILayout + the element,
        // parented under the canvas) so a canvas authored in the viewport is identical to one from the panel.
        entt::entity FindCanvas( entt::registry& reg )
        {
            auto view = reg.view<ECS::UICanvasComponent>();
            return view.begin() == view.end() ? entt::null : *view.begin();
        }

        template <typename ElementComponent>
        entt::entity AddUIChild( ::Desert::Core::Scene& scene, entt::entity parent, const char* name )
        {
            auto& e      = scene.CreateNewEntity( std::string( name ) );
            auto  handle = e.GetHandle();
            e.AddComponent<ECS::UILayoutComponent>();
            e.AddComponent<ElementComponent>();

            auto& reg = scene.GetRegistry();
            if ( !reg.has<ECS::RelationshipComponent>( handle ) )
                reg.emplace<ECS::RelationshipComponent>( handle );
            reg.get<ECS::RelationshipComponent>( handle ).Parent = parent;
            if ( !reg.has<ECS::RelationshipComponent>( parent ) )
                reg.emplace<ECS::RelationshipComponent>( parent );
            reg.get<ECS::RelationshipComponent>( parent ).Children.push_back( handle );
            return handle;
        }

        // Unity/UE-style anchor presets. Each axis is Min/Center/Max (a fixed-size box pinned to that edge,
        // keeping the element's current size) or Stretch (anchors 0..1, zero offsets -> fills the parent on
        // that axis). Stretch+Stretch = "fill parent" (the full-quad the UI needs). Offsets are in the same
        // screen-px space UICanvasRenderer resolves layout in.
        enum class AnchorAxis
        {
            Min,
            Center,
            Max,
            Stretch
        };

        void AnchorAxisValues( AnchorAxis m, float size, float& aMin, float& aMax, float& offMin, float& offMax )
        {
            switch ( m )
            {
                case AnchorAxis::Stretch:
                    aMin   = 0.0f;
                    aMax   = 1.0f;
                    offMin = 0.0f;
                    offMax = 0.0f;
                    break;
                case AnchorAxis::Min:
                    aMin   = 0.0f;
                    aMax   = 0.0f;
                    offMin = 0.0f;
                    offMax = size;
                    break;
                case AnchorAxis::Center:
                    aMin   = 0.5f;
                    aMax   = 0.5f;
                    offMin = -size * 0.5f;
                    offMax = size * 0.5f;
                    break;
                case AnchorAxis::Max:
                    aMin   = 1.0f;
                    aMax   = 1.0f;
                    offMin = -size;
                    offMax = 0.0f;
                    break;
            }
        }

        void ApplyAnchorPreset( entt::registry& reg, entt::entity e, const ::Desert::UI::Rect& viewRect,
                                AnchorAxis hx, AnchorAxis vy )
        {
            if ( !reg.has<ECS::UILayoutComponent>( e ) )
                return;
            auto& L = reg.get<ECS::UILayoutComponent>( e ).Data;

            // Element size in DESIGN space (the space UILayout offsets are stored in). GetElementRect returns
            // the on-screen rect, so divide by the canvas scale. Fall back to the element's authored size when
            // it isn't resolvable (e.g. not parented under a canvas yet) so Fill/presets never silently no-op.
            const float scale = ::Desert::UI::CanvasScale( reg, viewRect );
            const float inv   = scale > 0.0001f ? 1.0f / scale : 1.0f;
            float       sizeX = std::max( 1.0f, L.OffsetMax.x - L.OffsetMin.x );
            float       sizeY = std::max( 1.0f, L.OffsetMax.y - L.OffsetMin.y );
            if ( ::Desert::UI::Rect er; ::Desert::UI::GetElementRect( reg, e, viewRect, er ) )
            {
                sizeX = er.W * inv;
                sizeY = er.H * inv;
            }

            float axMin, axMax, oMinX, oMaxX, ayMin, ayMax, oMinY, oMaxY;
            AnchorAxisValues( hx, sizeX, axMin, axMax, oMinX, oMaxX );
            AnchorAxisValues( vy, sizeY, ayMin, ayMax, oMinY, oMaxY );
            L.AnchorMin = glm::vec2( axMin, ayMin );
            L.AnchorMax = glm::vec2( axMax, ayMax );
            L.OffsetMin = glm::vec2( oMinX, oMinY );
            L.OffsetMax = glm::vec2( oMaxX, oMaxY );
        }

        ImGuiMouseCursor CursorForHandle( UIHandle h )
        {
            switch ( h )
            {
                case UIHandle::L:
                case UIHandle::R:
                    return ImGuiMouseCursor_ResizeEW;
                case UIHandle::T:
                case UIHandle::B:
                    return ImGuiMouseCursor_ResizeNS;
                case UIHandle::TL:
                case UIHandle::BR:
                    return ImGuiMouseCursor_ResizeNWSE;
                case UIHandle::TR:
                case UIHandle::BL:
                    return ImGuiMouseCursor_ResizeNESW;
                case UIHandle::Body:
                    return ImGuiMouseCursor_ResizeAll;
                case UIHandle::AnchorMin:
                case UIHandle::AnchorMax:
                    return ImGuiMouseCursor_Hand;
                default:
                    return ImGuiMouseCursor_Arrow;
            }
        }
    } // namespace

    ViewportPanel::ViewportPanel( const std::shared_ptr<Desert::Core::Scene>& scene,
                                  const Assets::AssetManager* assetManager, std::string title )
         : IPanel( std::move( title ) ), m_Scene( scene ), m_AssetManager( assetManager )
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
            ImGui::SetNextWindowPos(
                 ImVec2( m_ViewportData.ViewportPos.x + 16.0f, m_ViewportData.ViewportPos.y + 16.0f ) );
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

        // --- In-scene UI authoring (Godot/UE-style): create + parent UI elements without the UI Editor panel.
        // New elements parent under the selected UI element if one is selected, else under the canvas. ---
        {
            ImGui::SameLine();
            if ( ImGui::Button( ICON_MDI_VIEW_DASHBOARD "  UI" ) )
                ImGui::OpenPopup( "ui_create" );
            if ( ImGui::BeginPopup( "ui_create" ) )
            {
                auto&              reg    = m_Scene->GetRegistry();
                const entt::entity canvas = FindCanvas( reg );
                if ( canvas == entt::null )
                {
                    if ( ImGui::MenuItem( ICON_MDI_PLUS "  UI Canvas" ) )
                    {
                        auto& e = m_Scene->CreateNewEntity( "UI Canvas" );
                        e.AddComponent<ECS::UICanvasComponent>();
                        Core::SelectionManager::SetSelected( e.GetComponent<ECS::UUIDComponent>().UUID );
                    }
                }
                else
                {
                    entt::entity parent = canvas;
                    if ( const auto& sel = Core::SelectionManager::GetSelected(); sel.has_value() )
                        if ( auto ref = m_Scene->FindEntityByID( *sel ) )
                        {
                            const entt::entity h = ref->get().GetHandle();
                            if ( h == canvas || reg.has<ECS::UILayoutComponent>( h ) )
                                parent = h; // nest under the selected element
                        }
                    const auto select = [&]( entt::entity h )
                    { Core::SelectionManager::SetSelected( reg.get<ECS::UUIDComponent>( h ).UUID ); };
                    if ( ImGui::MenuItem( ICON_MDI_CARD_OUTLINE "  Panel" ) )
                        select( AddUIChild<ECS::UIPanelComponent>( *m_Scene, parent, "UI Panel" ) );
                    if ( ImGui::MenuItem( ICON_MDI_FORMAT_TEXT "  Text" ) )
                        select( AddUIChild<ECS::UITextComponent2D>( *m_Scene, parent, "UI Text" ) );
                    if ( ImGui::MenuItem( ICON_MDI_BUTTON_POINTER "  Button" ) )
                        select( AddUIChild<ECS::UIButtonComponent>( *m_Scene, parent, "UI Button" ) );
                    if ( ImGui::MenuItem( ICON_MDI_IMAGE "  Image" ) )
                        select( AddUIChild<ECS::UIImageComponent>( *m_Scene, parent, "UI Image" ) );
                    if ( ImGui::MenuItem( ICON_MDI_VIEW_GRID "  Layout Group" ) )
                        select( AddUIChild<ECS::UILayoutGroupComponent>( *m_Scene, parent, "UI Layout Group" ) );
                    if ( ImGui::MenuItem( ICON_MDI_PROGRESS_HELPER "  Progress Bar" ) )
                        select( AddUIChild<ECS::UIProgressBarComponent>( *m_Scene, parent, "UI Progress Bar" ) );
                    if ( ImGui::MenuItem( ICON_MDI_CHECKBOX_MARKED_OUTLINE "  Toggle" ) )
                        select( AddUIChild<ECS::UIToggleComponent>( *m_Scene, parent, "UI Toggle" ) );
                    if ( ImGui::MenuItem( ICON_MDI_TUNE_VARIANT "  Slider" ) )
                        select( AddUIChild<ECS::UISliderComponent>( *m_Scene, parent, "UI Slider" ) );
                    if ( ImGui::MenuItem( ICON_MDI_VIEW_LIST "  Scroll View" ) )
                        select( AddUIChild<ECS::UIScrollViewComponent>( *m_Scene, parent, "UI Scroll View" ) );
                    if ( ImGui::MenuItem( ICON_MDI_FORM_TEXTBOX "  Input Field" ) )
                        select( AddUIChild<ECS::UIInputFieldComponent>( *m_Scene, parent, "UI Input Field" ) );
                    if ( ImGui::MenuItem( ICON_MDI_MENU_DOWN "  Dropdown" ) )
                        select( AddUIChild<ECS::UIDropdownComponent>( *m_Scene, parent, "UI Dropdown" ) );
                }
                ImGui::EndPopup();
            }
        }

        // --- In-scene UI: anchor presets (Unity/UE RectTransform) + 2D toggle. Shown only when relevant so
        // the toolbar stays clean for pure-3D scenes. ---
        {
            auto&              reg    = m_Scene->GetRegistry();
            const entt::entity canvas = FindCanvas( reg );
            entt::entity       selUI  = entt::null;
            if ( const auto& sel = Core::SelectionManager::GetSelected(); sel.has_value() )
                if ( auto ref = m_Scene->FindEntityByID( *sel ) )
                    if ( reg.has<ECS::UILayoutComponent>( ref->get().GetHandle() ) )
                        selUI = ref->get().GetHandle();

            const ::Desert::UI::Rect viewRect{ m_ViewportData.ViewportPos.x, m_ViewportData.ViewportPos.y,
                                               m_ViewportData.Size.x, m_ViewportData.Size.y };

            if ( selUI != entt::null && viewRect.W > 1.0f )
            {
                ImGui::SameLine();
                if ( ImGui::Button( ICON_MDI_ARROW_EXPAND_ALL "  Fill" ) )
                    ApplyAnchorPreset( reg, selUI, viewRect, AnchorAxis::Stretch, AnchorAxis::Stretch );
                if ( ImGui::IsItemHovered() )
                    ImGui::SetTooltip( "Stretch to fill the parent (anchors 0,0 - 1,1, offsets 0)" );

                ImGui::SameLine();
                if ( ImGui::Button( ICON_MDI_ANCHOR "  Anchors" ) )
                    ImGui::OpenPopup( "ui_anchors" );
                if ( ImGui::BeginPopup( "ui_anchors" ) )
                {
                    const AnchorAxis modes[4] = { AnchorAxis::Min, AnchorAxis::Center, AnchorAxis::Max,
                                                  AnchorAxis::Stretch };
                    const char*      cl[4]    = { "L", "C", "R", "<->" };
                    const char*      rl[4]    = { "T", "M", "B", "^v" };
                    ImGui::TextDisabled( "Anchor preset (keeps size; stretch fills the axis)" );
                    for ( int r = 0; r < 4; ++r )
                        for ( int c = 0; c < 4; ++c )
                        {
                            if ( c > 0 )
                                ImGui::SameLine();
                            const std::string lbl =
                                 std::string( rl[r] ) + cl[c] + "##a" + std::to_string( r * 4 + c );
                            if ( ImGui::Button( lbl.c_str(), ImVec2( 40.0f, 28.0f ) ) )
                            {
                                ApplyAnchorPreset( reg, selUI, viewRect, modes[c], modes[r] );
                                ImGui::CloseCurrentPopup();
                            }
                        }
                    ImGui::EndPopup();
                }
            }

            if ( canvas != entt::null )
            {
                ImGui::SameLine();
                if ( ImGui::Checkbox( "2D", &m_UIMode ) )
                {
                    auto& s = m_Scene->GetSettings();
                    if ( m_UIMode )
                    {
                        m_SavedShowGrid = s.ShowGrid;
                        s.ShowGrid      = false;
                    }
                    else
                    {
                        s.ShowGrid = m_SavedShowGrid;
                    }
                }
                if ( ImGui::IsItemHovered() )
                    ImGui::SetTooltip( "2D UI mode: hide the grid + orientation gizmo" );

                // Design <-> Preview: Preview feeds the viewport mouse/keyboard into the canvas so buttons /
                // toggles / sliders react in the editor (like UMG preview); Design keeps drag/select/handles.
                ImGui::SameLine();
                const bool prevActive = m_UIPreview;
                if ( prevActive )
                    ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.85f, 0.45f, 0.1f, 1.0f ) );
                if ( ImGui::Button( m_UIPreview ? ICON_MDI_PLAY "  Preview"
                                                : ICON_MDI_CURSOR_DEFAULT_OUTLINE "  Design" ) )
                    m_UIPreview = !m_UIPreview;
                if ( prevActive )
                    ImGui::PopStyleColor();
                if ( ImGui::IsItemHovered() )
                    ImGui::SetTooltip( m_UIPreview
                                            ? "Preview: UI is interactive (click buttons). Toggle for Design."
                                            : "Design: drag/select UI. Toggle for interactive Preview." );
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

        // Multi-scene: focusing this viewport makes ITS scene the active one (Outliner/Details/gizmo
        // follow). Idempotent on the editor side, so calling it every focused frame is fine.
        if ( m_OnActivate && ImGui::IsWindowFocused() )
            m_OnActivate();

        // Feed the fly-camera its input gate: WASD/QE/arrows work while the viewport is hovered
        // and no text field owns the keyboard (otherwise typing "wasd" in a search box flies away).
        if ( auto* editorCam = dynamic_cast<::Desert::Core::EditorCamera*>( mainCamera.get() ) )
        {
            editorCam->SetInputEnabled( m_ViewportData.IsHovered && !ImGui::GetIO().WantTextInput );
        }

        // Render scene
        m_UIHelper->Image( m_Scene->GetFinalImage(), { m_ViewportData.Size.x, m_ViewportData.Size.y } );

        // UI Preview vs Design. Preview: publish the viewport pointer/keyboard so the EditorUIPass drives the
        // canvas with real input (buttons interactive) and SKIP the authoring overlays/handles. Design: the
        // in-scene WYSIWYG handles (select marquee + drag/resize/anchor) as before.
        {
            auto& pv = ::Desert::Editor::Core::UIPreview::Get();
            if ( m_UIPreview )
            {
                const bool down = m_ViewportData.IsHovered && ImGui::IsMouseDown( ImGuiMouseButton_Left );
                pv.Enabled      = true;
                pv.HasInput     = m_ViewportData.IsHovered;
                pv.MousePx      = m_ViewportData.MousePosition;
                pv.DisplaySize  = m_ViewportData.Size;
                pv.Released     = pv.Down && !down; // down->up edge
                pv.Down         = down;
                pv.Scroll       = m_ViewportData.IsHovered ? ImGui::GetIO().MouseWheel : 0.0f;
                pv.Tab          = ImGui::IsKeyPressed( ImGuiKey_Tab, false );
                pv.Submit       = ImGui::IsKeyPressed( ImGuiKey_Enter, false );
                pv.Backspace    = ImGui::IsKeyPressed( ImGuiKey_Backspace, false );
                pv.TypedText.clear();
                for ( ImWchar c : ImGui::GetIO().InputQueueCharacters )
                    if ( c >= 32 && c < 128 ) // ASCII typed chars (matches the default font atlas)
                        pv.TypedText.push_back( static_cast<char>( c ) );
            }
            else
            {
                pv.Enabled = false;
                DrawUIInScene();
            }
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

        // UI elements are edited with the in-scene UILayout handles (DrawUIInScene), not the 3D transform
        // gizmo — suppress the object gizmo for them so the two don't overlap and fight for the mouse.
        bool selectedIsUI = false;
        if ( const auto& sel = Core::SelectionManager::GetSelected(); sel.has_value() )
            if ( auto ref = m_Scene->FindEntityByID( *sel ) )
                selectedIsUI = m_Scene->GetRegistry().has<ECS::UILayoutComponent>( ref->get().GetHandle() );

        // Handle gizmos (Select mode only — Foliage mode uses LMB to paint, not to gizmo/pick).
        m_Gizmo.ResetHovered();
        if ( !foliageMode )
        {
            if ( Core::SkeletonEditMode::IsActive() )
            {
                // skeleton edit owns the gizmo (edits the selected bone, not the object)
                m_Gizmo.RenderBone( *m_Scene, m_ViewportData.ViewportPos, m_ViewportData.Size );
            }
            else if ( m_Gizmo.IsActive() && !painting && !selectedIsUI )
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

        // Corner XYZ orientation triad — a 3D aid, so hide it in 2D UI mode (like Unity's 2D scene view).
        if ( !m_UIMode )
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

    void ViewportPanel::DrawUIInScene()
    {
        if ( !m_Scene )
            return;

        auto&                    reg = m_Scene->GetRegistry();
        const ::Desert::UI::Rect viewRect{ m_ViewportData.ViewportPos.x, m_ViewportData.ViewportPos.y,
                                           m_ViewportData.Size.x, m_ViewportData.Size.y };
        ImDrawList*              dl = ImGui::GetWindowDrawList();

        // The canvas CONTENT is drawn by the engine's own Render2D batcher (EditorUIPass, into the scene image
        // shown here) — no longer re-drawn with ImGui. This function now only adds the editor-authoring
        // overlays (canvas bounds, selection handles, anchor markers) on top of that image.

        // Canvas bounds outline so an EMPTY canvas (no Panel yet) is still visible + selectable — you can
        // see where it maps on screen. Dashed-ish subtle frame, drawn under the element handles.
        if ( const entt::entity canvas = FindCanvas( reg ); canvas != entt::null )
        {
            ::Desert::UI::Rect cr;
            if ( ::Desert::UI::GetElementRect( reg, canvas, viewRect, cr ) )
                dl->AddRect( ImVec2( cr.X, cr.Y ), ImVec2( cr.X + cr.W, cr.Y + cr.H ),
                             IM_COL32( 120, 135, 160, 170 ), 0.0f, 0, 1.5f );
        }

        // Editing handles only for a selected UI element.
        const auto& sel = Core::SelectionManager::GetSelected();
        if ( !sel.has_value() )
            return;
        auto ref = m_Scene->FindEntityByID( *sel );
        if ( !ref )
            return;
        const entt::entity e = ref->get().GetHandle();
        if ( !reg.has<ECS::UILayoutComponent>( e ) )
            return;

        ::Desert::UI::Rect r;
        if ( !::Desert::UI::GetElementRect( reg, e, viewRect, r ) )
            return;

        // Selection marquee.
        dl->AddRect( ImVec2( r.X, r.Y ), ImVec2( r.X + r.W, r.Y + r.H ), IM_COL32( 255, 170, 40, 255 ), 0.0f, 0,
                     2.0f );

        // Edit only in Select mode, over the viewport, and not while grabbing a 3D gizmo.
        const bool canEdit = Core::ViewportMode::Get() == Core::EditorMode::Select && m_ViewportData.IsHovered &&
                             !m_Gizmo.IsHovered();

        // 8 resize handles (corners + edge midpoints), screen px.
        const float cx = r.X + r.W * 0.5f, cy = r.Y + r.H * 0.5f;
        struct HandlePt
        {
            UIHandle Id;
            ImVec2   P;
        };
        const HandlePt handles[8] = {
             { UIHandle::TL, ImVec2( r.X, r.Y ) },
             { UIHandle::T, ImVec2( cx, r.Y ) },
             { UIHandle::TR, ImVec2( r.X + r.W, r.Y ) },
             { UIHandle::R, ImVec2( r.X + r.W, cy ) },
             { UIHandle::BR, ImVec2( r.X + r.W, r.Y + r.H ) },
             { UIHandle::B, ImVec2( cx, r.Y + r.H ) },
             { UIHandle::BL, ImVec2( r.X, r.Y + r.H ) },
             { UIHandle::L, ImVec2( r.X, cy ) },
        };
        const float hs = 4.0f; // half handle size

        if ( canEdit )
            for ( const auto& h : handles )
            {
                dl->AddRectFilled( ImVec2( h.P.x - hs, h.P.y - hs ), ImVec2( h.P.x + hs, h.P.y + hs ),
                                   IM_COL32( 255, 170, 40, 255 ) );
                dl->AddRect( ImVec2( h.P.x - hs, h.P.y - hs ), ImVec2( h.P.x + hs, h.P.y + hs ),
                             IM_COL32( 20, 20, 20, 255 ) );
            }

        const ImVec2 mouse = ImGui::GetMousePos();
        const float  scale = std::max( 0.0001f, ::Desert::UI::CanvasScale( reg, viewRect ) );

        // Parent rect: anchors are fractions of it. Draggable anchor markers let you re-anchor the element
        // (change how it pins to the parent) WITHOUT moving it — same idea as Unity's RectTransform anchors.
        const entt::entity parentE = reg.has<ECS::RelationshipComponent>( e )
                                          ? reg.get<ECS::RelationshipComponent>( e ).Parent
                                          : entt::null;
        ::Desert::UI::Rect pr;
        const bool         haveParent =
             parentE != entt::null && ::Desert::UI::GetElementRect( reg, parentE, viewRect, pr );

        const auto&  aL    = reg.get<ECS::UILayoutComponent>( e ).Data;
        const ImVec2 aMinP = haveParent ? ImVec2( pr.X + aL.AnchorMin.x * pr.W, pr.Y + aL.AnchorMin.y * pr.H )
                                        : ImVec2( 0.0f, 0.0f );
        const ImVec2 aMaxP = haveParent ? ImVec2( pr.X + aL.AnchorMax.x * pr.W, pr.Y + aL.AnchorMax.y * pr.H )
                                        : ImVec2( 0.0f, 0.0f );
        const float  ar    = 5.0f;
        if ( canEdit && haveParent )
        {
            dl->AddCircleFilled( aMinP, ar, IM_COL32( 90, 200, 255, 235 ) );
            dl->AddCircle( aMinP, ar, IM_COL32( 15, 15, 15, 255 ) );
            dl->AddCircleFilled( aMaxP, ar, IM_COL32( 90, 200, 255, 235 ) );
            dl->AddCircle( aMaxP, ar, IM_COL32( 15, 15, 15, 255 ) );
        }

        // Handle under the cursor this frame (drives the cursor + starts a drag). Anchor markers first, then
        // the resize handles, then the body.
        UIHandle hovered = UIHandle::None;
        if ( canEdit && haveParent )
        {
            const float ah = ar + 2.0f;
            if ( std::abs( mouse.x - aMaxP.x ) <= ah && std::abs( mouse.y - aMaxP.y ) <= ah )
                hovered = UIHandle::AnchorMax;
            else if ( std::abs( mouse.x - aMinP.x ) <= ah && std::abs( mouse.y - aMinP.y ) <= ah )
                hovered = UIHandle::AnchorMin;
        }
        if ( hovered == UIHandle::None )
            for ( const auto& h : handles )
                if ( mouse.x >= h.P.x - hs - 1.0f && mouse.x <= h.P.x + hs + 1.0f &&
                     mouse.y >= h.P.y - hs - 1.0f && mouse.y <= h.P.y + hs + 1.0f )
                {
                    hovered = h.Id;
                    break;
                }
        if ( hovered == UIHandle::None && mouse.x >= r.X && mouse.x <= r.X + r.W && mouse.y >= r.Y &&
             mouse.y <= r.Y + r.H )
            hovered = UIHandle::Body;

        if ( canEdit && m_UIDrag == UIHandle::None && hovered != UIHandle::None &&
             ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
        {
            m_UIDrag            = hovered;
            m_UIDragStartMouse  = glm::vec2( mouse.x, mouse.y );
            const auto& L       = reg.get<ECS::UILayoutComponent>( e ).Data;
            m_UIDragStartOffMin = L.OffsetMin;
            m_UIDragStartOffMax = L.OffsetMax;
            m_UIDragStartRect   = glm::vec4( r.X, r.Y, r.X + r.W, r.Y + r.H ); // edges to preserve on re-anchor
        }

        // Dragging an anchor marker re-anchors the element (changes AnchorMin/Max) while KEEPING its on-screen
        // rect — offsets are recomputed to hold the preserved edges. Anchors snap to 0/0.5/1 (Alt disables).
        if ( m_UIDrag == UIHandle::AnchorMin || m_UIDrag == UIHandle::AnchorMax )
        {
            if ( ImGui::IsMouseDown( ImGuiMouseButton_Left ) && haveParent && pr.W > 0.0f && pr.H > 0.0f )
            {
                auto& L  = reg.get<ECS::UILayoutComponent>( e ).Data;
                float fx = std::clamp( ( mouse.x - pr.X ) / pr.W, 0.0f, 1.0f );
                float fy = std::clamp( ( mouse.y - pr.Y ) / pr.H, 0.0f, 1.0f );
                if ( !ImGui::GetIO().KeyAlt )
                    for ( float s : { 0.0f, 0.5f, 1.0f } )
                    {
                        if ( std::abs( fx - s ) < 0.03f )
                            fx = s;
                        if ( std::abs( fy - s ) < 0.03f )
                            fy = s;
                    }
                if ( m_UIDrag == UIHandle::AnchorMin )
                {
                    fx          = std::min( fx, L.AnchorMax.x );
                    fy          = std::min( fy, L.AnchorMax.y );
                    L.OffsetMin = glm::vec2( ( m_UIDragStartRect.x - pr.X - fx * pr.W ) / scale,
                                             ( m_UIDragStartRect.y - pr.Y - fy * pr.H ) / scale );
                    L.AnchorMin = glm::vec2( fx, fy );
                }
                else
                {
                    fx          = std::max( fx, L.AnchorMin.x );
                    fy          = std::max( fy, L.AnchorMin.y );
                    L.OffsetMax = glm::vec2( ( m_UIDragStartRect.z - pr.X - fx * pr.W ) / scale,
                                             ( m_UIDragStartRect.w - pr.Y - fy * pr.H ) / scale );
                    L.AnchorMax = glm::vec2( fx, fy );
                }
            }
            ImGui::SetMouseCursor( ImGuiMouseCursor_Hand );
            if ( ImGui::IsMouseReleased( ImGuiMouseButton_Left ) )
                m_UIDrag = UIHandle::None;
        }
        else if ( m_UIDrag != UIHandle::None )
        {
            if ( ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
            {
                glm::vec2 d = ( glm::vec2( mouse.x, mouse.y ) - m_UIDragStartMouse ) / scale; // design px
                if ( ImGui::GetIO().KeyShift && m_UIDrag == UIHandle::Body ) // Shift = lock the dominant axis
                {
                    if ( std::abs( d.x ) >= std::abs( d.y ) )
                        d.y = 0.0f;
                    else
                        d.x = 0.0f;
                }

                const bool eL = m_UIDrag == UIHandle::L || m_UIDrag == UIHandle::TL || m_UIDrag == UIHandle::BL;
                const bool eR = m_UIDrag == UIHandle::R || m_UIDrag == UIHandle::TR || m_UIDrag == UIHandle::BR;
                const bool eT = m_UIDrag == UIHandle::T || m_UIDrag == UIHandle::TL || m_UIDrag == UIHandle::TR;
                const bool eB = m_UIDrag == UIHandle::B || m_UIDrag == UIHandle::BL || m_UIDrag == UIHandle::BR;

                glm::vec2 oMin = m_UIDragStartOffMin, oMax = m_UIDragStartOffMax;
                if ( m_UIDrag == UIHandle::Body )
                {
                    oMin += d; // move: shift both edges so the element slides, whatever its anchors
                    oMax += d;
                }
                else
                {
                    if ( eL )
                        oMin.x += d.x;
                    if ( eR )
                        oMax.x += d.x;
                    if ( eT )
                        oMin.y += d.y;
                    if ( eB )
                        oMax.y += d.y;
                }

                // Snapping: align the element's edges/centre to the parent's edges/centre (hold Alt to
                // disable). Computed in screen space, the winning snap is converted back to design offsets,
                // and a cyan guide line is drawn. The dragged edge(s) / whole box shift onto the guide.
                if ( !ImGui::GetIO().KeyAlt )
                {
                    const entt::entity parent = reg.has<ECS::RelationshipComponent>( e )
                                                     ? reg.get<ECS::RelationshipComponent>( e ).Parent
                                                     : entt::null;
                    ::Desert::UI::Rect pr;
                    if ( parent != entt::null && ::Desert::UI::GetElementRect( reg, parent, viewRect, pr ) )
                    {
                        const auto& L0     = reg.get<ECS::UILayoutComponent>( e ).Data;
                        const float eLeft  = pr.X + L0.AnchorMin.x * pr.W + oMin.x * scale;
                        const float eRight = pr.X + L0.AnchorMax.x * pr.W + oMax.x * scale;
                        const float eTop   = pr.Y + L0.AnchorMin.y * pr.H + oMin.y * scale;
                        const float eBot   = pr.Y + L0.AnchorMax.y * pr.H + oMax.y * scale;
                        const float gx[3]  = { pr.X, pr.X + pr.W * 0.5f, pr.X + pr.W };
                        const float gy[3]  = { pr.Y, pr.Y + pr.H * 0.5f, pr.Y + pr.H };
                        const float thr    = 6.0f;
                        const bool  move   = m_UIDrag == UIHandle::Body;

                        struct Cand
                        {
                            float Pos;
                            bool  Min;
                            bool  Max;
                        };

                        Cand xs[3];
                        int  nx = 0;
                        if ( move )
                        {
                            xs[nx++] = { eLeft, true, true };
                            xs[nx++] = { ( eLeft + eRight ) * 0.5f, true, true };
                            xs[nx++] = { eRight, true, true };
                        }
                        else
                        {
                            if ( eL )
                                xs[nx++] = { eLeft, true, false };
                            if ( eR )
                                xs[nx++] = { eRight, false, true };
                        }
                        float bestX = thr, gXpos = 0.0f, addX = 0.0f;
                        bool  hitX = false, minX = false, maxX = false;
                        for ( int i = 0; i < nx; ++i )
                            for ( float g : gx )
                                if ( std::abs( xs[i].Pos - g ) < bestX )
                                {
                                    bestX = std::abs( xs[i].Pos - g );
                                    addX  = ( g - xs[i].Pos ) / scale;
                                    minX  = xs[i].Min;
                                    maxX  = xs[i].Max;
                                    gXpos = g;
                                    hitX  = true;
                                }
                        if ( hitX )
                        {
                            if ( minX )
                                oMin.x += addX;
                            if ( maxX )
                                oMax.x += addX;
                            dl->AddLine( ImVec2( gXpos, r.Y - 40.0f ), ImVec2( gXpos, r.Y + r.H + 40.0f ),
                                         IM_COL32( 90, 200, 255, 200 ), 1.0f );
                        }

                        Cand ys[3];
                        int  ny = 0;
                        if ( move )
                        {
                            ys[ny++] = { eTop, true, true };
                            ys[ny++] = { ( eTop + eBot ) * 0.5f, true, true };
                            ys[ny++] = { eBot, true, true };
                        }
                        else
                        {
                            if ( eT )
                                ys[ny++] = { eTop, true, false };
                            if ( eB )
                                ys[ny++] = { eBot, false, true };
                        }
                        float bestY = thr, gYpos = 0.0f, addY = 0.0f;
                        bool  hitY = false, minY = false, maxY = false;
                        for ( int i = 0; i < ny; ++i )
                            for ( float g : gy )
                                if ( std::abs( ys[i].Pos - g ) < bestY )
                                {
                                    bestY = std::abs( ys[i].Pos - g );
                                    addY  = ( g - ys[i].Pos ) / scale;
                                    minY  = ys[i].Min;
                                    maxY  = ys[i].Max;
                                    gYpos = g;
                                    hitY  = true;
                                }
                        if ( hitY )
                        {
                            if ( minY )
                                oMin.y += addY;
                            if ( maxY )
                                oMax.y += addY;
                            dl->AddLine( ImVec2( r.X - 40.0f, gYpos ), ImVec2( r.X + r.W + 40.0f, gYpos ),
                                         IM_COL32( 90, 200, 255, 200 ), 1.0f );
                        }
                    }
                }

                auto& L     = reg.get<ECS::UILayoutComponent>( e ).Data;
                L.OffsetMin = oMin;
                L.OffsetMax = oMax;
            }
            ImGui::SetMouseCursor( CursorForHandle( m_UIDrag ) );
            if ( ImGui::IsMouseReleased( ImGuiMouseButton_Left ) )
                m_UIDrag = UIHandle::None;
        }
        else if ( canEdit && hovered != UIHandle::None )
        {
            ImGui::SetMouseCursor( CursorForHandle( hovered ) );
        }
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

        if ( e.GetMouseButton() == Common::MouseButton::Left && !m_TerrainTool.BrushEnabled() && !m_UIPreview &&
             Core::ViewportMode::Get() == Core::EditorMode::Select && m_ViewportData.IsHovered && overImage )
        {
            // In-scene UI: the 2D canvas overlays the 3D scene, so a click on a UI element selects it and
            // skips the 3D raycast (unless a 3D gizmo handle is being grabbed). Edit anchors/colour in Details.
            if ( !m_Gizmo.IsHovered() )
            {
                auto&        reg   = m_Scene->GetRegistry();
                entt::entity uiHit = ::Desert::UI::PickElement(
                     reg, glm::vec2( mp.x, mp.y ),
                     ::Desert::UI::Rect{ vp.ViewportPos.x, vp.ViewportPos.y, vp.Size.x, vp.Size.y } );

                // Clicking a control's content (e.g. a button's label / icon) selects the CONTROL, not the
                // child — promote the hit to its nearest interactable ancestor. Alt-click drills down to the
                // exact element under the cursor instead.
                if ( uiHit != entt::null && !::ImGui::GetIO().KeyAlt )
                {
                    auto isInteractable = [&]( entt::entity x )
                    {
                        return reg.has<ECS::UIButtonComponent>( x ) || reg.has<ECS::UIToggleComponent>( x ) ||
                               reg.has<ECS::UISliderComponent>( x ) || reg.has<ECS::UIDropdownComponent>( x ) ||
                               reg.has<ECS::UIInputFieldComponent>( x );
                    };
                    for ( entt::entity cur = uiHit; cur != entt::null; )
                    {
                        if ( isInteractable( cur ) )
                        {
                            uiHit = cur;
                            break;
                        }
                        cur = reg.has<ECS::RelationshipComponent>( cur )
                                   ? reg.get<ECS::RelationshipComponent>( cur ).Parent
                                   : entt::null;
                    }
                }

                if ( uiHit != entt::null && reg.has<ECS::UUIDComponent>( uiHit ) )
                {
                    const auto uuid = reg.get<ECS::UUIDComponent>( uiHit ).UUID;
                    if ( ::ImGui::GetIO().KeyCtrl )
                        Core::SelectionManager::Toggle( uuid );
                    else
                        Core::SelectionManager::SetSelected( uuid );
                    return false;
                }
            }

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
