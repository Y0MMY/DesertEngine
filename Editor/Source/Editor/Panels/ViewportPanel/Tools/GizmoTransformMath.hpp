#pragma once

#include <glm/glm.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Desert::Editor::Tools
{
    // ── WHY THIS IS A SEPARATE, PURE HEADER ──────────────────────────────────────────────────────────
    //
    // The gizmo's write-back is three lines inside a function that needs a Scene, a camera, an ImGui
    // draw list and a live ImGuizmo frame — so the ONE part of it that can actually be wrong by itself,
    // the matrix composition, was unreachable by any test. It is lifted here unchanged in behaviour and
    // nothing else: GizmoController calls this, and so does the GizmoTransformSpace suite.
    //
    // What can be wrong: a manipulated WORLD matrix is turned back into the entity's LOCAL translation /
    // euler rotation / scale, and a TRS triple cannot represent a sheared matrix. glm::decompose hands
    // the shear back in its own out-parameter, and the call site used to drop it on the floor. Dropping
    // it is not automatically wrong — see Skew below — but it has to be VISIBLE to be judged, and a
    // discarded local is not.
    struct LocalTRS
    {
        glm::vec3 Translation{ 0.0f };
        glm::vec3 Rotation{ 0.0f }; // euler radians, the form TransformComponent stores
        glm::vec3 Scale{ 1.0f };

        // The shear the TRS triple above CANNOT carry, in glm::decompose's order (XY, XZ, YZ).
        //
        // This is the number that separates the two transform spaces, and it is why the suite asserts a
        // RELATION rather than a spot value. Scale a rotated object along a WORLD axis and the result is
        // genuinely sheared: no translation, rotation and scale exist that reproduce it, so something
        // must be thrown away and the object visibly changes shape in a way the handles did not promise.
        // Scale it along its OWN axes and the shear is exactly zero, so the same discard is lossless.
        // ImGuizmo forces Local for scale for this reason (ImGuizmo.cpp:2653), and Core::GizmoState
        // reports that through EffectiveSpace() so the toolbar cannot claim otherwise.
        glm::vec3 Skew{ 0.0f };

        // True when this triple reproduces the matrix it came from — i.e. nothing was silently dropped.
        bool IsLossless( float epsilon = 1.0e-4f ) const
        {
            return glm::all( glm::lessThan( glm::abs( Skew ), glm::vec3( epsilon ) ) );
        }
    };

    // Converts a gizmo-manipulated WORLD matrix into the entity's LOCAL TRS, given its parent's world
    // matrix (identity for a root entity, so this is a no-op there). This is the exact conversion the
    // object gizmo performs on every manipulated frame.
    inline LocalTRS WorldToLocalTRS( const glm::mat4& parentWorld, const glm::mat4& worldMatrix )
    {
        const glm::mat4 localMatrix = glm::inverse( parentWorld ) * worldMatrix;

        glm::vec3 scale, translation, skew;
        glm::quat rotation;
        glm::vec4 perspective;
        glm::decompose( localMatrix, scale, rotation, translation, skew, perspective );

        LocalTRS out;
        out.Translation = translation;
        out.Rotation    = glm::eulerAngles( rotation );
        out.Scale       = scale;
        out.Skew        = skew;
        return out;
    }
} // namespace Desert::Editor::Tools
