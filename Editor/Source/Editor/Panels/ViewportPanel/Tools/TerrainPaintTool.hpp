#pragma once

#include <Engine/Desert.hpp>
#include <Common/Core/Math/Ray.hpp>

#include <glm/glm.hpp>

namespace Desert::Editor::Tools
{
    // Terrain splat-layer painting (grass/rock/snow weights into the terrain's splat map), extracted from
    // ViewportPanel (god-object split). Owns the brush state; the host passes the cursor ray + the selected
    // terrain entity. This is the GROUND-MATERIAL brush — NOT vegetation (that's FoliagePaintTool).
    class TerrainPaintTool
    {
    public:
        // Whether the brush is on (the host suppresses object pick/gizmo while painting).
        [[nodiscard]] bool BrushEnabled() const { return m_Brush.Enabled; }

        // Floating overlay (brush settings) — call inside the viewport window when a terrain is selected.
        void DrawOverlay( const ECS::Entity& terrain );

        // World-space radius ring at the cursor (viewProj = camera proj*view; viewport rect for projection).
        void DrawRing( const Common::Math::Ray& ray, const ECS::Entity& terrain, const glm::mat4& viewProj,
                       const glm::vec2& viewportSize, const glm::vec2& viewportPos );

        // Stamp the brush into the terrain's splat map at the cursor.
        void Paint( const Common::Math::Ray& ray, const ECS::Entity& terrain );

        // Upload any dirty splat maps to the GPU (call from OnPreUpdate — recreates sampled images, must not
        // race in-flight work).
        void UploadDirtySplatMaps( ::Desert::Core::Scene& scene );

    private:
        // Cursor ray vs the horizontal plane at the terrain's base height -> world point. false if no hit.
        bool PickPoint( const Common::Math::Ray& ray, const ECS::Entity& terrain, glm::vec3& outHit ) const;

        struct Brush
        {
            bool  Enabled  = false;
            int   Layer    = 0;    // 0 = grass (R), 1 = rock (G), 2 = snow (B)
            float Radius   = 6.0f; // world meters
            float Strength = 0.6f; // 0..1 per application
            bool  Erase    = false;
        };
        Brush m_Brush;
    };
} // namespace Desert::Editor::Tools
