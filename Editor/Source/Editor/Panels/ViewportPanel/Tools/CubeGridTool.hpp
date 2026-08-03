#pragma once

#include <Common/Core/UUID.hpp>
#include <Common/Core/Math/Ray.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Desert::Core
{
    class Scene;
}

namespace Desert::Editor::Tools
{
    // UE5-style CubeGrid blockout tool (Modeling mode). Build grid-aligned boxes: LMB on the ground (or on an
    // existing box's face) drops a W×D grid footprint, then drag along the face normal to STRETCH it to a
    // free, continuous height (a deformation — NOT a stack of unit cubes). The height is a real world-space
    // value editable afterwards (panel field / E-Q / arrows) which re-deforms the last box live, before
    // Accept. The blockout is a LIVE DynamicMesh scene entity that regenerates on every edit; Accept keeps it
    // and starts a fresh one, Cancel deletes it.
    class CubeGridTool
    {
    public:
        // Per-frame entry while Modeling mode is active. `viewProj` = projection*view; `ray` is the world ray
        // under the cursor; `interactive` = viewport hovered (gates mouse/keys).
        void Update( ::Desert::Core::Scene& scene, const Common::Math::Ray& ray, const glm::mat4& viewProj,
                     const glm::vec2& viewportPos, const glm::vec2& viewportSize, bool interactive );

    private:
        // A grid-aligned footprint (W×D cells on a plane) extruded a FREE, continuous distance along a face
        // normal — the box stretches smoothly. `cellMin` is meaningful only on the two in-plane axes; the
        // normal-axis position is the float `base` plane, and it extends `sign*height` world units from it.
        struct Box
        {
            glm::ivec3 cellMin{ 0 };  // footprint min-corner cell (in-plane axes)
            int        w = 1, d = 1;  // footprint size in cells (grid-aligned)
            int        na     = 1;    // normal axis (0=X, 1=Y, 2=Z)
            int        sign   = 1;    // outward direction along na (+1 / -1)
            float      base   = 0.0f; // world coord of the footprint plane along na
            float      height = 0.0f; // world extrude length along the outward normal (free / continuous)
        };

        void        RegenMesh( ::Desert::Core::Scene& scene ); // rebuild the DynamicMesh from the boxes
        void        Cancel( ::Desert::Core::Scene& scene );    // delete the in-progress blockout entity
        static bool WorldToScreen( const glm::vec3& world, const glm::mat4& vp, const glm::vec2& pos,
                                   const glm::vec2& size, glm::vec2& out );

        std::vector<Box> m_Boxes;                           // committed boxes
        float            m_CellSize = 1.0f;                 // grid step (mirrors ModelingState)
        Common::UUID     m_Entity   = Common::UUID::Null(); // live blockout entity

        // Frame targeting: the footprint plane under the cursor (nearest box face, else the ground y=0).
        bool       m_HasTarget = false;
        glm::ivec3 m_TargetCell{ 0 };
        int        m_TargetNa   = 1;
        int        m_TargetSign = 1;
        float      m_TargetBase = 0.0f;
        bool       m_HasErase   = false; // a whole box under the cursor (Shift/RMB removes it)
        int        m_EraseBox   = -1;

        // Extrude drag: footprint fixed at press, height set CONTINUOUSLY in screen space.
        bool      m_Dragging = false;
        Box       m_DragBox;
        glm::vec2 m_DragStartMouse{ 0 };
        float     m_DragBaseHeight = 0.0f;

        int m_LastBox = -1; // index of the last committed box — Height edits re-deform it live before Accept
    };
} // namespace Desert::Editor::Tools
