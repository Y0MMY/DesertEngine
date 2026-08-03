#pragma once

#include <Common/Core/UUID.hpp>
#include <Common/Core/Math/Ray.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <unordered_set>

namespace Desert::Core
{
    class Scene;
}

namespace Desert::Editor::Tools
{
    // UE5-style CubeGrid blockout tool (Modeling mode). PAINT the grid: hold LMB and sweep the cursor over the
    // ground to fill W×D cells (you build as much as you move — Shift/RMB erases). The whole painted footprint
    // is extruded to a FREE, continuous Height (world units) set from the panel / E-Q / arrows — NOT from the
    // mouse. The blockout is a LIVE DynamicMesh scene entity that regenerates on every edit (internal walls
    // culled -> one clean slab); Accept keeps it and starts a fresh one, Cancel deletes it.
    class CubeGridTool
    {
    public:
        // Per-frame entry while Modeling mode is active. `viewProj` = projection*view; `ray` is the world ray
        // under the cursor; `interactive` = viewport hovered (gates mouse/keys).
        void Update( ::Desert::Core::Scene& scene, const Common::Math::Ray& ray, const glm::mat4& viewProj,
                     const glm::vec2& viewportPos, const glm::vec2& viewportSize, bool interactive );

    private:
        void        RegenMesh( ::Desert::Core::Scene& scene ); // rebuild the DynamicMesh from painted cells
        void        Cancel( ::Desert::Core::Scene& scene );    // delete the in-progress blockout entity
        static bool WorldToScreen( const glm::vec3& world, const glm::mat4& vp, const glm::vec2& pos,
                                   const glm::vec2& size, glm::vec2& out );

        std::unordered_set<uint64_t> m_Cells;              // painted ground cells (packed ix,iz)
        float                        m_CellSize   = 1.0f;  // grid step (mirrors ModelingState)
        float                        m_LastHeight = -1.0f; // last baked Height (detects panel edits)
        Common::UUID                 m_Entity     = Common::UUID::Null(); // live blockout entity

        // Frame targeting: the ground cell under the cursor.
        bool       m_HasCell = false;
        glm::ivec2 m_Cell{ 0 };

        // Paint stroke: sweep-fill while a mouse button is held; cells between frames are interpolated so a
        // fast drag leaves no gaps.
        bool       m_Painting     = false;
        bool       m_Erasing      = false;
        bool       m_HasLastPaint = false;
        glm::ivec2 m_LastPaintCell{ 0 };
    };
} // namespace Desert::Editor::Tools
