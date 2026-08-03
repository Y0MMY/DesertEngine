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
        float                        m_CellSize = 100.0f;               // grid step (Ctrl+E/Q => x2 / /2)
        Common::UUID                 m_Entity   = Common::UUID::Null(); // live blockout entity

        bool       m_HasAdd    = false;
        bool       m_HasRemove = false;
        glm::ivec3 m_Add{ 0 };    // cell a click would ADD
        glm::ivec3 m_Remove{ 0 }; // cell a click would REMOVE
    };
} // namespace Desert::Editor::Tools
