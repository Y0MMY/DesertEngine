#pragma once

#include <Common/Core/UUID.hpp>
#include <Common/Core/Math/Ray.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <unordered_map>
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

        // Corner Mode moves a cell's corners along the grid's up axis, so a cell is not a plain solid flag
        // but a box with 8 vertical corner offsets. Offsets are in 1/CornerDen of a BASE cell (60 divides
        // the ½ / ¼ / ¹⁄₁₀ snap sizes exactly), 0 everywhere = the ordinary axis-aligned block.
        // Corner index bits: 1 = +X, 2 = +Y (top), 4 = +Z.
        static constexpr int CornerDen = 60;
        struct Cell
        {
            int16_t V[8] = {};

            bool IsFlat() const
            {
                for ( int16_t o : V )
                    if ( o != 0 )
                        return false;
                return true;
            }
        };
        using CellMap = std::unordered_map<uint64_t, Cell>;

    private:
        void        RegenMesh( ::Desert::Core::Scene& scene );                // rebuild the DynamicMesh from cells
        void        Cancel( ::Desert::Core::Scene& scene );                   // delete the in-progress blockout
        void        PushPull( ::Desert::Core::Scene& scene, int dir, int K ); // extrude the selection out/in
        void        RefineBy( int F ); // subdivide the base grid by F (split every cell + the selection ×F)
        void        FreezeActive();    // commit the volume being worked on into an immutable layer
        void        RescaleSelection( float oldUnit, float newUnit, int K ); // keep the marquee in place
        // Occupancy across every layer, for a query cell of edge `unit` in the frame `origin`.
        bool SolidAt( const glm::ivec3& cell, float unit, const glm::vec3& origin ) const;
        // Is face `f` of `cell` hidden by its neighbour? A deformed cell only hides a face when the four
        // shared corners agree, otherwise the two boxes don't actually meet there.
        bool FaceHidden( const CellMap& cells, const glm::ivec3& cell, const Cell& data, int f, float unit,
                         const glm::vec3& origin ) const;
        // Write the Corner Mode heights into the top layer of cells under the selection (bilinear over the
        // rectangle, so picking two corners and raising them yields a clean ramp).
        void        ApplyCornerHeights( ::Desert::Core::Scene& scene );
        void        SyncCornerHeights(); // read the rectangle's corner heights back out of the cells
        static bool WorldToScreen( const glm::vec3& world, const glm::mat4& vp, const glm::vec2& pos,
                                   const glm::vec2& size, glm::vec2& out );

        // A committed piece. Its cells and its Block Size are frozen for good: starting a new marquee
        // commits what you already pushed out, so any later Resize Grid only ever re-scales the volume you
        // are working on NOW — never the geometry already built (UE: each Push produces finished blocks).
        struct Layer
        {
            CellMap   Cells;
            float     Unit = 1.0f;
            glm::vec3 Origin{ 0.0f }; // grid frame this piece was built in
        };
        std::vector<Layer> m_Frozen;

        CellMap      m_Cells;                            // BASE-resolution voxels (world = index*Unit)
        float        m_Unit      = -1.0f;                // base cell size; Block Size = K * m_Unit
        float        m_BakedUnit = -1.0f;                // base size the live mesh was last baked at
        Common::UUID m_Entity    = Common::UUID::Null(); // live blockout entity

        glm::vec3 m_ActiveOrigin{ 0.0f }; // grid frame the active volume is being built in
        float     m_GroundY    = 0.0f;    // ground work-plane height in the active grid frame (Level up/down)
        bool      m_HoverValid = false;   // last frame's cursor targeting hit something

        // Work-plane (in BASE cells): locked while selecting and kept for the active selection.
        int m_PlaneNa   = 1;
        int m_PlaneSign = 1;
        int m_PlaneCell = 0;

        // Marquee rectangle selection (inclusive BASE-cell range, always Block-aligned).
        bool       m_Selecting = false;
        glm::ivec2 m_Anchor{ 0 }; // press cell (base)
        bool       m_HasSel = false;
        int        m_UMin = 0, m_UMax = 0, m_VMin = 0, m_VMax = 0;

        // Corner Mode (Z): the selection rectangle's four corner posts. Heights are in the same
        // 1/CornerDen base-cell units as Cell::V; index order is (uMin,vMin) (uMax,vMin) (uMin,vMax)
        // (uMax,vMax).
        bool m_CornerMode = false;
        bool m_CornerSel[4]{};
        int  m_CornerH[4]{};
    };
} // namespace Desert::Editor::Tools
