// The DECISIONS the gameplay systems make, tested without a Scene.
//
// LocomotionSystem and AttachmentSystem both hold a `Core::Scene*`, and Scene drags in the renderer — so
// asking "does a character at 3 m/s pick the walk clip?" would otherwise mean linking the graphics stack
// and standing up a device. The rules live in Engine/ECS/System/SystemRules.hpp; the systems only fetch
// the arguments. Same split that made the shadow cascades testable.

#include <Engine/ECS/System/SystemRules.hpp>

#include <Common/Core/Units.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

using Desert::ECS::LocomotionComponent;
using Desert::ECS::Rules::DecomposeTransform;
using Desert::ECS::Rules::LocomotionClipFor;
using Desert::ECS::Rules::SocketLocalTransform;

namespace Units = Common::Units;

// ---------------------------------------------------------------------------------------------------
// Locomotion: speed -> clip name
// ---------------------------------------------------------------------------------------------------

TEST( LocomotionRules, PicksIdleWalkRunByTheComponentsThresholds )
{
    LocomotionComponent loco; // struct defaults ARE the data the system falls back to
    loco.WalkSpeed = 0.2f;
    loco.RunSpeed  = 6.5f;

    EXPECT_EQ( LocomotionClipFor( loco, 0.0f, true ), loco.IdleClip );
    EXPECT_EQ( LocomotionClipFor( loco, 0.19f, true ), loco.IdleClip );
    EXPECT_EQ( LocomotionClipFor( loco, 0.2f, true ), loco.WalkClip ) << "the threshold itself walks";
    EXPECT_EQ( LocomotionClipFor( loco, 3.0f, true ), loco.WalkClip );
    EXPECT_EQ( LocomotionClipFor( loco, 6.5f, true ), loco.WalkClip ) << "run starts ABOVE RunSpeed";
    EXPECT_EQ( LocomotionClipFor( loco, 6.6f, true ), loco.RunClip );
    EXPECT_EQ( LocomotionClipFor( loco, 40.0f, true ), loco.RunClip );
}

// Airborne beats any ground speed — the ordering IS the rule, and getting it backwards gives a character
// who runs in mid-air.
TEST( LocomotionRules, AirborneWinsOverEverySpeed )
{
    LocomotionComponent loco;
    for ( const float speed : { 0.0f, 0.5f, 5.0f, 100.0f } )
        EXPECT_EQ( LocomotionClipFor( loco, speed, /*onGround=*/false ), loco.JumpClip );
}

// The names come from the component, not from the system: a project that renames its clips must not need
// an engine change.
TEST( LocomotionRules, ClipNamesComeFromTheComponent )
{
    LocomotionComponent loco;
    loco.IdleClip = "Stand";
    loco.WalkClip = "Stroll";
    loco.RunClip  = "Sprint";
    loco.JumpClip = "Leap";

    EXPECT_EQ( LocomotionClipFor( loco, 0.0f, true ), "Stand" );
    EXPECT_EQ( LocomotionClipFor( loco, 1.0f, true ), "Stroll" );
    EXPECT_EQ( LocomotionClipFor( loco, 99.0f, true ), "Sprint" );
    EXPECT_EQ( LocomotionClipFor( loco, 1.0f, false ), "Leap" );
}

// ---------------------------------------------------------------------------------------------------
// Sockets: bone -> attached entity transform
// ---------------------------------------------------------------------------------------------------

namespace
{
    // Where the socket ends up in WORLD space (the rule returns a parent-local transform).
    glm::vec3 WorldPositionOf( const glm::mat4& local, const glm::mat4& parentWorld = glm::mat4( 1.0f ) )
    {
        return glm::vec3( parentWorld * local * glm::vec4( 0, 0, 0, 1 ) );
    }
} // namespace

TEST( SocketRules, PutsTheEntityOnTheBone )
{
    // Character at x = 5 m; the hand bone is 1.5 m up and 0.3 m out in model space.
    const glm::mat4 targetWorld =
         glm::translate( glm::mat4( 1.0f ), glm::vec3( Units::Metres( 5.0f ), 0.0f, 0.0f ) );
    const glm::mat4 boneModel =
         glm::translate( glm::mat4( 1.0f ), glm::vec3( Units::Metres( 0.3f ), Units::Metres( 1.5f ), 0.0f ) );

    const glm::mat4 local =
         SocketLocalTransform( targetWorld, boneModel, glm::vec3( 0.0f ), glm::vec3( 0.0f ), glm::vec3( 1.0f ) );
    const glm::vec3 world = WorldPositionOf( local );

    EXPECT_NEAR( world.x, Units::Metres( 5.3f ), 0.01f );
    EXPECT_NEAR( world.y, Units::Metres( 1.5f ), 0.01f );
    EXPECT_NEAR( world.z, 0.0f, 0.01f );
}

TEST( SocketRules, GripOffsetIsAppliedInBoneSpace )
{
    // Bone rotated 90° about +Y: an offset along +X must come out along -Z in world space.
    const glm::mat4 targetWorld( 1.0f );
    const glm::mat4 boneModel = glm::rotate( glm::mat4( 1.0f ), glm::radians( 90.0f ), glm::vec3( 0, 1, 0 ) );

    const glm::mat4 local =
         SocketLocalTransform( targetWorld, boneModel, glm::vec3( Units::Metres( 1.0f ), 0.0f, 0.0f ),
                               glm::vec3( 0.0f ), glm::vec3( 1.0f ) );
    const glm::vec3 world = WorldPositionOf( local );

    EXPECT_NEAR( world.x, 0.0f, 0.5f );
    EXPECT_NEAR( world.z, -Units::Metres( 1.0f ), 0.5f );
}

// A parented weapon must be brought back into its parent's space — otherwise it doubles the parent's
// motion and drifts off into the distance as the parent moves.
TEST( SocketRules, ParentSpaceIsRemovedFromTheResult )
{
    const glm::mat4 parentWorld =
         glm::translate( glm::mat4( 1.0f ), glm::vec3( Units::Metres( 10.0f ), 0.0f, 0.0f ) );
    const glm::mat4 targetWorld =
         glm::translate( glm::mat4( 1.0f ), glm::vec3( Units::Metres( 2.0f ), 0.0f, 0.0f ) );
    const glm::mat4 boneModel( 1.0f );

    const glm::mat4 local = SocketLocalTransform( targetWorld, boneModel, glm::vec3( 0.0f ), glm::vec3( 0.0f ),
                                                  glm::vec3( 1.0f ), parentWorld );

    // Local sits 8 m BEHIND the parent, so that parent * local lands on the bone at 2 m.
    EXPECT_NEAR( local[3].x, Units::Metres( -8.0f ), 0.01f );
    EXPECT_NEAR( WorldPositionOf( local, parentWorld ).x, Units::Metres( 2.0f ), 0.01f );
}

TEST( SocketRules, DecomposeRoundTripsTranslationScaleAndRotation )
{
    const glm::vec3 translation( Units::Metres( 1.0f ), Units::Metres( 2.0f ), Units::Metres( -3.0f ) );
    const glm::vec3 scale( 2.0f, 2.0f, 2.0f );
    const float     yaw = glm::radians( 35.0f );

    const glm::mat4 m = glm::translate( glm::mat4( 1.0f ), translation ) *
                        glm::rotate( glm::mat4( 1.0f ), yaw, glm::vec3( 0, 1, 0 ) ) *
                        glm::scale( glm::mat4( 1.0f ), scale );

    const auto out = DecomposeTransform( m );
    EXPECT_NEAR( out.Translation.x, translation.x, 0.01f );
    EXPECT_NEAR( out.Translation.y, translation.y, 0.01f );
    EXPECT_NEAR( out.Translation.z, translation.z, 0.01f );
    EXPECT_NEAR( out.Scale.x, scale.x, 0.01f );
    // Rotation comes back in RADIANS, like TransformComponent stores it — a degree here would be the
    // whole bug of "the weapon is rotated 57 times too far".
    EXPECT_NEAR( out.Rotation.y, yaw, 0.01f );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
