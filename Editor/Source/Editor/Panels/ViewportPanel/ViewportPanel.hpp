#pragma once

#include <functional>
#include <optional>

#include <Engine/Desert.hpp>

#include "Editor/Panels/IPanel.hpp"

#include "Editor/Widgets/UIHelper/ImGuiUI.hpp"

#include "LightGizmoRenderer.hpp"
#include "PerfHudOverlay.hpp"
#include "Tools/FoliagePaintTool.hpp"
#include "Tools/CubeGridTool.hpp"
#include "Tools/TerrainPaintTool.hpp"
#include "Tools/GizmoController.hpp"
#include "Tools/PickingController.hpp"

namespace Desert::Editor
{
    class AsyncMeshLoader; // async cook of dropped meshes (defined in Import/AsyncMeshLoader.hpp)

    // Which handle of a selected UI element is being dragged in the viewport (in-scene UI editing).
    // Body = move; the rest are the 8 resize handles (corners + edge midpoints).
    enum class UIHandle
    {
        None,
        Body,
        L,
        R,
        T,
        B,
        TL,
        TR,
        BL,
        BR,
        AnchorMin, // draggable anchor markers on the parent rect (re-anchor without moving the element)
        AnchorMax
    };

    class ViewportPanel : public IPanel, public Common::EventHandler
    {
    public:
        // `title` is the ImGui window title/id. Multi-scene editing spawns extra viewports, so each needs
        // its own unique "###id" (two windows sharing one id merge into a single dockable window).
        ViewportPanel( const std::shared_ptr<Desert::Core::Scene>& scene,
                       const Assets::AssetManager* assetManager = nullptr, std::string title = "Scene###scene" );
        ~ViewportPanel() override; // defined in the .cpp (unique_ptr<AsyncMeshLoader> needs the complete type)
        void OnUIRender() override;
        void OnPreUpdate() override;

        void OnEvent( Common::Event& e ) override;

        // Called (once, while this viewport window has ImGui focus) so the editor can make this viewport's
        // scene the active one — the Outliner/Details/gizmo then follow whichever viewport you work in.
        void SetOnActivate( std::function<void()> cb )
        {
            m_OnActivate = std::move( cb );
        }

    private:
        bool OnWindowResize( Common::EventWindowResize& e );
        bool OnMousePressed( Common::MouseButtonPressedEvent& e );
        bool OnKeyPressedEvent( Common::KeyPressedEvent& e );

    private:
        // Viewport data access
        const glm::vec2& GetSize() const
        {
            return m_ViewportData.Size;
        }
        bool IsHovered() const
        {
            return m_ViewportData.IsHovered;
        }

    private:
        // On spawning a mesh, auto-assign a "sidecar" material so packs come with their look: looks for
        // <stem>.demat next to the mesh, else any *.demat in the mesh's folder, else in the parent folder
        // (the collection root). Assigns it to every material slot. No-op if none found.
        void ApplySidecarMaterial( ECS::Entity& entity, const std::string& meshSourcePath );

        // Viewport material DnD: raycast the mesh under the cursor and assign the dropped .demat
        // to its material elements (all of them — the hit carries no submesh id yet).
        void AssignMaterialAtCursor( const std::string& materialPath );

        // Godot-style toolbar row ABOVE the image: mode, transform tools, snap, contextual
        // skeleton toggle, camera gear (right). Replaces the old floating in-viewport overlay.
        void DrawViewportToolbar();

        // Corner XYZ orientation gizmo (a small triad tracking the camera's rotation) so you always know
        // which way world X/Y/Z point in the current view. Overlay only — pure ImGui, no scene interaction.
        void DrawViewAxisGizmo( const glm::vec2& viewportPos, const glm::vec2& viewportSize );

        // Draws the active scene's UICanvas over the viewport + (for a selected UI element) a selection marquee
        // and 8 drag/resize handles, and applies mouse drag to its UILayout offsets. In-scene UI editing.
        void DrawUIInScene();

    private:
        std::pair<float, float> GetMouseViewportSpace() const;

        struct ViewportData
        {
            glm::vec2 MousePosition;
            glm::vec2 Size;
            glm::vec2 ViewportPos;
            bool      IsHovered = false;
            float     DpiScale  = 1.0f;
        };

        ViewportData m_ViewportData;

        // True while the cursor is over the corner view-axis gizmo — set in DrawViewAxisGizmo, read in
        // OnMousePressed to suppress scene picking (a click there snaps the camera, it doesn't select).
        bool m_ViewAxisGizmoHovered = false;

        // 2D UI-editing mode (toolbar "2D"): hides the grid + orientation gizmo so a screen-space canvas
        // reads like a UI designer. m_SavedShowGrid restores the scene's grid setting when toggled off.
        bool m_UIMode        = false;
        bool m_SavedShowGrid = true;
        bool m_UIPreview     = false; // Design (drag/select) <-> Preview (buttons interactive) toggle

        // In-scene UI drag/resize state. Offsets are captured at drag start so the drag is absolute (no drift).
        UIHandle  m_UIDrag = UIHandle::None;
        glm::vec2 m_UIDragStartMouse{};
        glm::vec2 m_UIDragStartOffMin{};
        glm::vec2 m_UIDragStartOffMax{};
        glm::vec4 m_UIDragStartRect{}; // element screen edges (left,top,right,bottom) at anchor-drag start

        // Resize is deferred from OnUIRender (within the recording window) to OnPreUpdate
        // (start of next frame, before any rendering) to avoid destroying descriptor set pools
        // while they are bound to a recording command buffer.
        std::optional<glm::vec2> m_PendingViewportSize;

        std::shared_ptr<Desert::Core::Scene>  m_Scene;
        std::function<void()>                 m_OnActivate; // fired while this viewport window is focused
        const Assets::AssetManager*           m_AssetManager = nullptr; // for prefab drag-drop instantiate
        std::unique_ptr<Editor::UI::UIHelper> m_UIHelper;
        std::unique_ptr<LightGizmoRenderer>   m_LightGizmoRenderer;
        PerfHudOverlay                        m_PerfHud; // View -> Perf HUD viewport overlay
        Tools::FoliagePaintTool               m_FoliageTool;  // UE5-style foliage painting (extracted)
        Tools::CubeGridTool                   m_CubeGridTool; // UE5-style CubeGrid blockout (Modeling mode)
        Tools::TerrainPaintTool               m_TerrainTool;  // terrain splat-layer painting (extracted)
        Tools::GizmoController                m_Gizmo;       // object + bone transform gizmos (extracted)
        Tools::PickingController              m_Picking;     // ray-pick + select (extracted)
        std::unique_ptr<AsyncMeshLoader>      m_AsyncLoader; // background cook of dropped meshes (no hitch)

        // Drain finished async cooks (main thread): register + assign the mesh to its pending entity. Called
        // once per frame from OnUIRender. Also draws the loading progress bar while cooks are in flight.
        void UpdateAsyncLoads();
    };
} // namespace Desert::Editor