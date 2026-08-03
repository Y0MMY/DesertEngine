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
    // UE5-style CubeGrid blockout tool (Modeling mode). A 3D grid of unit cells: PAINT on whatever surface is
    // under the cursor — the ground plane, or a FACE of the geometry you already built (top to stack up, sides
    // to extend out). A stroke locks its work-plane at the press, then sweeps to fill W×D footprints (Brush
    // Height cells thick); Shift/RMB erases. The blockout is a LIVE DynamicMesh scene entity that regenerates
    // on every edit (internal faces culled); Accept keeps it and starts a fresh one, Cancel deletes it.
    class CubeGridTool
    {
    public:
        // Per-frame entry while Modeling mode is active. `viewProj` = projection*view; `ray` is the world ray
        // under the cursor; `interactive` = viewport hovered (gates the START of mouse/keys).
        void Update( ::Desert::Core::Scene& scene, const Common::Math::Ray& ray, const glm::mat4& viewProj,
                     const glm::vec2& viewportPos, const glm::vec2& viewportSize, bool interactive );

    private:
        void        RegenMesh( ::Desert::Core::Scene& scene ); // rebuild the DynamicMesh from the cells
        void        Cancel( ::Desert::Core::Scene& scene );    // delete the in-progress blockout entity
        static bool WorldToScreen( const glm::vec3& world, const glm::mat4& vp, const glm::vec2& pos,
                                   const glm::vec2& size, glm::vec2& out );

        std::unordered_set<uint64_t> m_Cells;                         // occupied lattice cells (packed ivec3)
        glm::vec3                    m_CellSize{ 1.0f };              // per-axis box size W×H×D (mirrors state)
        glm::vec3                    m_BakedSize{ -1.0f };            // box size the live mesh was last baked at
        Common::UUID                 m_Entity = Common::UUID::Null(); // live blockout entity

        // Frame targeting: the surface under the cursor — the empty cell an ADD would fill (+ its face normal),
        // and the filled cell an ERASE would remove.
        bool       m_HasTarget = false;
        glm::ivec3 m_TargetCell{ 0 };
        glm::ivec3 m_TargetNormal{ 0, 1, 0 };
        bool       m_HasErase = false;
        glm::ivec3 m_EraseCell{ 0 };

        // Paint stroke: the work-plane is LOCKED at the press (ground, or the clicked face) so a sweep paints
        // one clean surface; cells between frames are interpolated so a fast drag leaves no gaps.
        bool       m_Painting        = false;
        bool       m_Erasing         = false;
        int        m_StrokeNa        = 1;    // locked plane's normal axis (0=X,1=Y,2=Z)
        int        m_StrokeSign      = 1;    // extrude direction along na (+1 / -1)
        int        m_StrokePlaneCell = 0;    // cell index along na the footprint fills
        float      m_StrokePlaneW    = 0.0f; // world coord of the locked plane along na
        bool       m_HasLastPaint    = false;
        glm::ivec2 m_LastPaintUV{ 0 }; // last painted (u,v) cell on the locked plane
    };
} // namespace Desert::Editor::Tools
