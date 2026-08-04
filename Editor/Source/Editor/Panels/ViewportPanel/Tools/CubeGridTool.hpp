#pragma once

#include <Common/Core/UUID.hpp>
#include <Common/Core/Math/Ray.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace Desert::Core
{
    class Scene;
}

namespace Desert::Editor::Tools
{
    // UE5-style CubeGrid blockout tool (Modeling mode). Marquee-select a rectangle on the work surface (the
    // ground, or a face of what you built), then Push / Pull to extrude that region. Blocks Per Step multiplies
    // the extrude height.
    //
    // KEY ARCHITECTURE (matches UE): solids are stored at a FIXED BASE resolution and NEVER move when the
    // Block Size (grid step) changes — changing Block Size only re-scales the brush/grid. A coarser Block Size
    // just stamps K×K×K base cells at once; a Block Size finer than the base subdivides the base losslessly
    // (each cell splits into F³, geometry stays put). So a big block and small blocks coexist in one mesh.
    class CubeGridTool
    {
    public:
        void Update( ::Desert::Core::Scene& scene, const Common::Math::Ray& ray, const glm::mat4& viewProj,
                     const glm::vec2& viewportPos, const glm::vec2& viewportSize, bool interactive );

    private:
        void        RegenMesh( ::Desert::Core::Scene& scene );                // rebuild the DynamicMesh from cells
        void        Cancel( ::Desert::Core::Scene& scene );                   // delete the in-progress blockout
        void        PushPull( ::Desert::Core::Scene& scene, int dir, int K ); // extrude the selection out/in
        void        RefineBy( int F ); // subdivide the base grid by F (split every cell + the selection ×F)
        void        FreezeActive();    // commit the volume being worked on into an immutable layer
        bool        SolidAt( const glm::ivec3& cell, float unit ) const; // occupancy across every layer
        static bool WorldToScreen( const glm::vec3& world, const glm::mat4& vp, const glm::vec2& pos,
                                   const glm::vec2& size, glm::vec2& out );

        // A committed piece. Its cells and its Block Size are frozen for good: starting a new marquee
        // commits what you already pushed out, so any later Resize Grid only ever re-scales the volume you
        // are working on NOW — never the geometry already built (UE: each Push produces finished blocks).
        struct Layer
        {
            std::unordered_set<uint64_t> Cells;
            float                        Unit = 1.0f;
        };
        std::vector<Layer> m_Frozen;

        std::unordered_set<uint64_t> m_Cells;             // BASE-resolution voxels (world = index*Unit)
        float                        m_Unit      = -1.0f; // base cell size; Block Size = K * m_Unit
        float                        m_BakedUnit = -1.0f; // base size the live mesh was last baked at
        Common::UUID                 m_Entity    = Common::UUID::Null(); // live blockout entity

        float m_GroundY    = 0.0f;  // ground work-plane height in WORLD units (Level up/down)
        bool  m_HoverValid = false; // last frame's cursor targeting hit something

        // Work-plane (in BASE cells): locked while selecting and kept for the active selection.
        int m_PlaneNa   = 1;
        int m_PlaneSign = 1;
        int m_PlaneCell = 0;

        // Marquee rectangle selection (inclusive BASE-cell range, always Block-aligned).
        bool       m_Selecting = false;
        glm::ivec2 m_Anchor{ 0 }; // press cell (base)
        bool       m_HasSel = false;
        int        m_UMin = 0, m_UMax = 0, m_VMin = 0, m_VMax = 0;
    };
} // namespace Desert::Editor::Tools
