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
    // UE5-style CubeGrid blockout tool (Modeling mode). Build grid-aligned box geometry by adding / removing
    // unit cells on a repositionable grid — LMB adds a cube on the targeted face (or the ground), Shift/RMB
    // removes the hovered cube, Ctrl+E / Ctrl+Q grow / shrink the grid step. The blockout is a LIVE
    // DynamicMesh scene entity that regenerates on every edit (internal faces culled); Accept keeps it and
    // starts a fresh one, Cancel deletes it. This reproduces the RESULT of UE5's CubeGrid (grid-aligned
    // greybox) with a robust voxel-cell model.
    class CubeGridTool
    {
    public:
        // Per-frame entry while Modeling mode is active. `viewProj` = projection*view (projects the cell
        // highlight to screen); `ray` is the world ray under the cursor; `interactive` = viewport hovered
        // (gates mouse/keys). Draws the highlight overlay + the tool panel and applies edits.
        void Update( ::Desert::Core::Scene& scene, const Common::Math::Ray& ray, const glm::mat4& viewProj,
                     const glm::vec2& viewportPos, const glm::vec2& viewportSize, bool interactive );

    private:
        void        RegenMesh( ::Desert::Core::Scene& scene ); // rebuild the DynamicMesh from the cells
        void        Cancel( ::Desert::Core::Scene& scene );    // delete the in-progress blockout entity
        static bool WorldToScreen( const glm::vec3& world, const glm::mat4& vp, const glm::vec2& pos,
                                   const glm::vec2& size, glm::vec2& out );

        std::unordered_set<uint64_t> m_Cells;                           // occupied grid cells (packed ivec3)
        float                        m_CellSize = 1.0f;                 // grid step (mirrors ModelingState)
        Common::UUID                 m_Entity   = Common::UUID::Null(); // live blockout entity

        // Frame targeting (nearest cube face / ground).
        bool       m_HasAdd    = false;
        bool       m_HasRemove = false;
        glm::ivec3 m_Add{ 0 };             // cell a click would ADD
        glm::ivec3 m_Remove{ 0 };          // cell a click would REMOVE
        glm::ivec3 m_AddNormal{ 0, 1, 0 }; // outward face normal of the add cell (extrude / drag-plane axis)

        // Extrude drag (LMB press on a cell, drag along the face normal to set the column HEIGHT in SCREEN
        // space, release to commit) — clean columns that never overlap existing cells.
        bool       m_Dragging = false;
        glm::ivec3 m_DragAnchor{ 0 };       // footprint anchor cell captured at press
        glm::ivec3 m_DragNormal{ 0, 1, 0 }; // extrude axis (the pressed face's outward normal)
        glm::vec2  m_DragStartMouse{ 0 };   // cursor position at press (screen-space height reference)
        int        m_DragBaseH = 1;         // column height at press (= ModelingState.Height), drag adds to it

        // Last committed column — its base footprint + normal + height. Editing ModelingState.Height (panel
        // field or E/Q) RE-EXTRUDES this column live, before Accept.
        std::vector<glm::ivec3> m_LastBase;
        glm::ivec3              m_LastNormal{ 0, 1, 0 };
        int                     m_LastHeight = 0;

        // Last placed region + its normal (legacy; kept for the free-form cell set).
        std::vector<glm::ivec3> m_Region;
        glm::ivec3              m_RegionNormal{ 0, 1, 0 };
    };
} // namespace Desert::Editor::Tools
