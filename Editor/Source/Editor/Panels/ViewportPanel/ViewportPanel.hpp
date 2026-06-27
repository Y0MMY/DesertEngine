#pragma once

#include <optional>

#include <Engine/Desert.hpp>

#include "Editor/Panels/IPanel.hpp"

#include "Editor/Widgets/UIHelper/ImGuiUI.hpp"

#include "LightGizmoRenderer.hpp"

namespace Desert::Editor
{
    class ViewportPanel : public IPanel, public Common::EventHandler
    {
    public:
        ViewportPanel( const std::shared_ptr<Desert::Core::Scene>& scene,
                       const Assets::AssetManager*                 assetManager = nullptr );
        void OnUIRender() override;
        void OnPreUpdate() override;

        void OnEvent( Common::Event& e ) override;

    private:
        bool OnWindowResize( Common::EventWindowResize& e );
        bool OnMousePressed( Common::MouseButtonPressedEvent& e );
        bool OnKeyPressedEvent( Common::KeyPressedEvent& e );

    private:
        // Gizmo functionality
        enum class GizmoType
        {
            None      = -1,
            Translate = 7,   // ImGuizmo::OPERATION::TRANSLATE
            Rotate    = 120, // ImGuizmo::OPERATION::ROTATE
            Scale     = 896, // ImGuizmo::OPERATION::SCALE
        };
        void SetGizmoType( GizmoType type )
        {
            m_GizmoType = type;
        }

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
        Mesh* GetMeshComponent( const ECS::StaticMeshComponent& component );

    private:
        void                    HandleObjectPicking();
        std::pair<float, float> GetMouseViewportSpace() const;
        void                    RenderGizmo();

        // --- Terrain splat painting (Stage 3b) ---
        void DrawTerrainPaintOverlay( const ECS::Entity& terrainEntity ); // brush UI when terrain selected
        void PaintTerrainAtCursor( const ECS::Entity& terrainEntity );    // ray->plane pick + stamp splat
        void DrawBrushRing( const ECS::Entity& terrainEntity );           // world-space radius ring at cursor
        void UploadDirtySplatMaps();                                      // safe GPU (re)upload (OnPreUpdate)
        // Mouse ray x horizontal plane at the terrain's base height -> world hit point. false if no hit.
        bool TerrainPickPoint( const ECS::Entity& terrainEntity, glm::vec3& outHit ) const;

        struct TerrainBrush
        {
            bool  Enabled  = false;
            int   Layer    = 0;    // 0 = grass (R), 1 = rock (G), 2 = snow (B)
            float Radius   = 6.0f; // world meters
            float Strength = 0.6f; // 0..1 per application
            bool  Erase    = false;
        };
        TerrainBrush m_TerrainBrush;

        struct ViewportData
        {
            glm::vec2 MousePosition;
            glm::vec2 Size;
            glm::vec2 ViewportPos;
            bool      IsHovered = false;
            float     DpiScale  = 1.0f;
        };

        ViewportData m_ViewportData;
        GizmoType    m_GizmoType    = GizmoType::None;
        bool         m_GizmoHovered = false;

        // Resize is deferred from OnUIRender (within the recording window) to OnPreUpdate
        // (start of next frame, before any rendering) to avoid destroying descriptor set pools
        // while they are bound to a recording command buffer.
        std::optional<glm::vec2> m_PendingViewportSize;

        std::shared_ptr<Desert::Core::Scene>  m_Scene;
        const Assets::AssetManager*           m_AssetManager = nullptr; // for prefab drag-drop instantiate
        std::unique_ptr<Editor::UI::UIHelper> m_UIHelper;
        std::unique_ptr<LightGizmoRenderer>   m_LightGizmoRenderer;
    };
} // namespace Desert::Editor