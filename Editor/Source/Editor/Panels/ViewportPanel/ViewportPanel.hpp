#pragma once

#include <optional>

#include <Engine/Desert.hpp>

#include "Editor/Panels/IPanel.hpp"

#include "Editor/Widgets/UIHelper/ImGuiUI.hpp"

#include "LightGizmoRenderer.hpp"
#include "PerfHudOverlay.hpp"
#include "Tools/FoliagePaintTool.hpp"
#include "Tools/TerrainPaintTool.hpp"
#include "Tools/GizmoController.hpp"
#include "Tools/PickingController.hpp"

namespace Desert::Editor
{
    class AsyncMeshLoader; // async cook of dropped meshes (defined in Import/AsyncMeshLoader.hpp)

    class ViewportPanel : public IPanel, public Common::EventHandler
    {
    public:
        ViewportPanel( const std::shared_ptr<Desert::Core::Scene>& scene,
                       const Assets::AssetManager*                 assetManager = nullptr );
        ~ViewportPanel() override; // defined in the .cpp (unique_ptr<AsyncMeshLoader> needs the complete type)
        void OnUIRender() override;
        void OnPreUpdate() override;

        void OnEvent( Common::Event& e ) override;

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

        // Resize is deferred from OnUIRender (within the recording window) to OnPreUpdate
        // (start of next frame, before any rendering) to avoid destroying descriptor set pools
        // while they are bound to a recording command buffer.
        std::optional<glm::vec2> m_PendingViewportSize;

        std::shared_ptr<Desert::Core::Scene>  m_Scene;
        const Assets::AssetManager*           m_AssetManager = nullptr; // for prefab drag-drop instantiate
        std::unique_ptr<Editor::UI::UIHelper> m_UIHelper;
        std::unique_ptr<LightGizmoRenderer>   m_LightGizmoRenderer;
        PerfHudOverlay                        m_PerfHud; // View -> Perf HUD viewport overlay
        Tools::FoliagePaintTool               m_FoliageTool; // UE5-style foliage painting (extracted)
        Tools::TerrainPaintTool               m_TerrainTool; // terrain splat-layer painting (extracted)
        Tools::GizmoController                m_Gizmo;       // object + bone transform gizmos (extracted)
        Tools::PickingController              m_Picking;     // ray-pick + select (extracted)
        std::unique_ptr<AsyncMeshLoader>      m_AsyncLoader; // background cook of dropped meshes (no hitch)

        // Drain finished async cooks (main thread): register + assign the mesh to its pending entity. Called
        // once per frame from OnUIRender. Also draws the loading progress bar while cooks are in flight.
        void UpdateAsyncLoads();
    };
} // namespace Desert::Editor