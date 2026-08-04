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
    // UE5-style CubeGrid blockout tool (Modeling mode). Flow (matches UE): drag a RECTANGLE on the work
    // surface (the ground, or a face of what you already built) to SELECT a grid region, then PUSH / PULL it
    // to extrude that region up / down along the surface normal. Resize Grid changes the block size. You can
    // re-select a region on any face of the object and push/pull it — all without leaving the tool. All
    // controls are on-screen buttons (no keyboard, to avoid clashing with the Q/E/arrow camera flight).
    class CubeGridTool
    {
    public:
        void Update( ::Desert::Core::Scene& scene, const Common::Math::Ray& ray, const glm::mat4& viewProj,
                     const glm::vec2& viewportPos, const glm::vec2& viewportSize, bool interactive );

    private:
        void        RegenMesh( ::Desert::Core::Scene& scene ); // rebuild the DynamicMesh from the cells
        void        Cancel( ::Desert::Core::Scene& scene );    // delete the in-progress blockout entity
        void        PushPull( ::Desert::Core::Scene& scene, int dir ); // extrude the selection out(+)/in(-)
        static bool WorldToScreen( const glm::vec3& world, const glm::mat4& vp, const glm::vec2& pos,
                                   const glm::vec2& size, glm::vec2& out );

        std::unordered_set<uint64_t> m_Cells;             // occupied grid cells (packed ivec3)
        glm::vec3                    m_Origin{ 0.0f };    // world pos of cell (0,0,0)'s min corner (resize pin)
        float                        m_BakedGrid = -1.0f; // grid size the live mesh was last baked at
        Common::UUID                 m_Entity    = Common::UUID::Null(); // live blockout entity

        // Work-plane: locked while selecting and kept for the active selection. `PlaneCell` is the cell index
        // along `PlaneNa` that a PUSH fills; `PlaneSign` is the outward normal direction.
        int m_PlaneNa   = 1;
        int m_PlaneSign = 1;
        int m_PlaneCell = 0;

        // Marquee rectangle selection (inclusive cell range on the plane's two in-plane axes).
        bool       m_Selecting = false;
        glm::ivec2 m_Anchor{ 0 }; // first cell of the drag
        bool       m_HasSel = false;
        int        m_UMin = 0, m_UMax = 0, m_VMin = 0, m_VMax = 0;
    };
} // namespace Desert::Editor::Tools
