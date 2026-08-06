#pragma once

#include <Engine/ECS/Components.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <string>

namespace Desert::ECS::Rules
{
    // The DECISIONS the gameplay systems make, as pure functions of their inputs.
    //
    // The systems themselves hold a `Core::Scene*`, and Scene drags in the renderer, so a test that wants
    // to ask "does a character at 3 m/s pick the walk clip?" would have to link the whole graphics stack
    // and stand up a device. The rule is the part worth testing; the system is the part that fetches the
    // arguments. Same split that made the shadow cascades testable.

    // Which locomotion clip a character should be playing. Ordering matters and is the rule itself:
    // airborne wins over any ground speed, then idle / walk / run by the component's own thresholds.
    inline const std::string& LocomotionClipFor( const LocomotionComponent& loco, float planarSpeed,
                                                 bool onGround )
    {
        if ( !onGround )
            return loco.JumpClip;
        if ( planarSpeed < loco.WalkSpeed )
            return loco.IdleClip;
        if ( planarSpeed <= loco.RunSpeed )
            return loco.WalkClip;
        return loco.RunClip;
    }

    // Where a socket-attached entity ends up: the bone's model-space transform lifted into the target's
    // world space, then the local grip offset. @p parentWorld is the attached entity's parent world matrix
    // (identity when it has none) — TransformComponent stores a LOCAL transform, so a parented weapon must
    // be brought back into its parent's space or it doubles the parent's motion.
    inline glm::mat4 SocketLocalTransform( const glm::mat4& targetWorld, const glm::mat4& boneModel,
                                           const glm::vec3& offsetTranslation, const glm::vec3& offsetRotation,
                                           const glm::vec3& offsetScale,
                                           const glm::mat4& parentWorld = glm::mat4( 1.0f ) )
    {
        const glm::mat4 offset = glm::translate( glm::mat4( 1.0f ), offsetTranslation ) *
                                 glm::toMat4( glm::quat( offsetRotation ) ) *
                                 glm::scale( glm::mat4( 1.0f ), offsetScale );
        return glm::inverse( parentWorld ) * targetWorld * boneModel * offset;
    }

    // Splits a transform matrix into the three fields TransformComponent stores (rotation in RADIANS,
    // like the component).
    struct DecomposedTransform
    {
        glm::vec3 Translation = glm::vec3( 0.0f );
        glm::vec3 Rotation    = glm::vec3( 0.0f ); // euler radians
        glm::vec3 Scale       = glm::vec3( 1.0f );
    };

    inline DecomposedTransform DecomposeTransform( const glm::mat4& m )
    {
        DecomposedTransform out;
        glm::vec3           skew;
        glm::vec4           perspective;
        glm::quat           rotation;
        glm::decompose( m, out.Scale, rotation, out.Translation, skew, perspective );
        out.Rotation = glm::eulerAngles( rotation );
        return out;
    }
} // namespace Desert::ECS::Rules
