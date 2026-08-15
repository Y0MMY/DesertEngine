// The lens flare's placement maths, driven against the shader's own text.
//
// Two halves are under test. Common/LensFlare.glslh, compiled here as C++, decides WHERE every feature
// lands; Engine/Graphic/PostProcessing/LensFlareRules.hpp decides how much of it is added back in. The
// second is one multiply and is tested anyway, because "the flare paints when the sun is behind the
// camera" is the way this effect goes wrong and the multiply is the only thing preventing it.
//
// The tests are relations, not spot values: ghosts in order along a named axis, a halo that reads the
// ring it draws, streak taps that overlap the source instead of stamping copies of it. Each of those is
// a property no single rendered frame can establish, and the two that were violated by the first
// implementation (dashed streak, halo sampling the empty far side) are pinned by name below.

#include "LensFlareReference.hpp"

#include <Engine/Graphic/PostProcessing/LensFlareRules.hpp>
#include <Engine/Graphic/PostProcessing/LightShaftRules.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <cmath>

using namespace Desert::Tests::LensFlareRef;

using Desert::Graphic::ComputeSunScreen;
using Desert::Graphic::LensFlareStrength;
using Desert::Graphic::SunScreen;

namespace
{
    constexpr float kAspect = 1280.0f / 766.0f; // the shot resolution these were tuned at
    const glm::vec2 kCentre( 0.5f, 0.5f );

    float Dist( const glm::vec2& a, const glm::vec2& b, float aspect )
    {
        return LensFlareDistance( a, b, aspect );
    }
} // namespace

// --- Ghost placement --------------------------------------------------------------------------------

TEST( LensFlareGhosts, WalkTheSunToCentreAxisInOrder )
{
    const glm::vec2 sun( 0.8f, 0.2f );
    const float     spacing = 0.35f;

    // Every ghost is ON the sun->centre line, and each is further along it than the last. "On the line"
    // is the whole claim of the feature: a ghost off the axis is not a reflection about the optical
    // centre, it is a sprite someone placed.
    float previous = -1.0f;
    for ( int i = 0; i < 6; ++i )
    {
        const glm::vec2 centre = LensFlareGhostCenter( sun, static_cast<float>( i ), spacing );

        // Colinearity via the 2D cross product of (centre - sun) and (kCentre - sun).
        const glm::vec2 toGhost  = centre - sun;
        const glm::vec2 toCentre = kCentre - sun;
        const float     cross    = toGhost.x * toCentre.y - toGhost.y * toCentre.x;
        EXPECT_NEAR( cross, 0.0f, 1e-6f ) << "ghost " << i << " left the sun->centre axis";

        const float along = glm::dot( toGhost, toCentre ) / glm::dot( toCentre, toCentre );
        EXPECT_GT( along, previous ) << "ghost " << i << " did not advance along the axis";
        previous = along;

        EXPECT_NEAR( along, spacing * static_cast<float>( i + 1 ), 1e-5f );
    }
}

TEST( LensFlareGhosts, ContinuePastTheCentreOnceSpacingExceedsTheAxis )
{
    // The second half of a real ghost train is on the far side of the optical centre. With spacing 0.35
    // that starts at ghost 2 (3 x 0.35 > 1), and the sign of (centre - screenCentre) must flip.
    const glm::vec2 sun( 0.8f, 0.2f );

    const glm::vec2 near = LensFlareGhostCenter( sun, 1.0f, 0.35f ); // 0.70 of the way
    const glm::vec2 far  = LensFlareGhostCenter( sun, 2.0f, 0.35f ); // 1.05 of the way

    EXPECT_GT( near.x, kCentre.x ); // still on the sun's side
    EXPECT_LT( far.x, kCentre.x );  // past it
}

TEST( LensFlareGhosts, SizeAndTintRampTogetherAcrossTheTrain )
{
    // Size and tint share LensFlareGhostRamp precisely so they cannot disagree about which end of the
    // train a ghost is on — the "two implementations of one quantity" defect this project keeps paying
    // for. Ghost 0 is exactly the near end, the last exactly the far end, and both are monotone.
    const float count = 5.0f;
    const vec3  inner( 1.0f, 0.86f, 0.62f );
    const vec3  outer( 0.45f, 0.68f, 1.0f );

    EXPECT_FLOAT_EQ( LensFlareGhostScale( 0.0f, count, 1.0f, 3.0f ), 1.0f );
    EXPECT_FLOAT_EQ( LensFlareGhostScale( count - 1.0f, count, 1.0f, 3.0f ), 3.0f );

    const vec3 first = LensFlareGhostTint( 0.0f, count, inner, outer );
    const vec3 last  = LensFlareGhostTint( count - 1.0f, count, inner, outer );
    EXPECT_NEAR( first.r, inner.r, 1e-6f );
    EXPECT_NEAR( last.b, outer.b, 1e-6f );

    float previousScale = -1.0f;
    for ( int i = 0; i < 5; ++i )
    {
        const float index = static_cast<float>( i );
        const float scale = LensFlareGhostScale( index, count, 1.0f, 3.0f );
        EXPECT_GT( scale, previousScale );
        previousScale = scale;

        // The tint's own position on the ramp must equal the size's, exactly.
        const vec3  tint     = LensFlareGhostTint( index, count, inner, outer );
        const float sizeRamp = ( scale - 1.0f ) / 2.0f;
        const float tintRamp = ( tint.r - inner.r ) / ( outer.r - inner.r );
        EXPECT_NEAR( sizeRamp, tintRamp, 1e-5f );
    }
}

TEST( LensFlareGhosts, ASingleGhostSitsAtTheNearEndOfTheRamp )
{
    // count == 1 divides by (count - 1). It must not produce NaN, and one ghost is the NEAR end.
    EXPECT_FLOAT_EQ( LensFlareGhostRamp( 0.0f, 1.0f ), 0.0f );
    EXPECT_FLOAT_EQ( LensFlareGhostScale( 0.0f, 1.0f, 1.0f, 3.0f ), 1.0f );
}

TEST( LensFlareGhosts, ReadTheSunAtTheirOwnCentreAndScaleAwayFromIt )
{
    // A ghost is an IMAGE of the source: at its centre it reads the sun, and its magnification is what
    // scale means. Both halves matter — the first is why a ghost is bright at all, the second is why a
    // larger ghost is the same picture enlarged rather than a bigger blob.
    const glm::vec2 sun( 0.8f, 0.2f );
    const glm::vec2 centre = LensFlareGhostCenter( sun, 0.0f, 0.35f );

    const glm::vec2 atCentre = LensFlareGhostSourceUv( centre, centre, 2.0f, sun );
    EXPECT_NEAR( atCentre.x, sun.x, 1e-6f );
    EXPECT_NEAR( atCentre.y, sun.y, 1e-6f );

    // Twice the scale reaches half as far into the source for the same screen offset.
    const glm::vec2 offset( 0.05f, 0.0f );
    const glm::vec2 small = LensFlareGhostSourceUv( centre + offset, centre, 1.0f, sun );
    const glm::vec2 large = LensFlareGhostSourceUv( centre + offset, centre, 2.0f, sun );
    EXPECT_NEAR( large.x - sun.x, ( small.x - sun.x ) * 0.5f, 1e-6f );
}

TEST( LensFlareGhosts, WeightIsZeroOutsideTheSourceFrame )
{
    // The engine-wide sampler is REPEAT, so a source uv outside [0,1] would wrap the opposite edge's
    // bright content back in. The weight, not the sampler, is what stops that.
    const glm::vec2 sun( 0.8f, 0.2f );
    const glm::vec2 centre = LensFlareGhostCenter( sun, 0.0f, 0.35f );

    EXPECT_FLOAT_EQ( LensFlareGhostWeight( glm::vec2( 1.4f, 0.5f ), centre, centre, 1.0f, kAspect ), 0.0f );
    EXPECT_FLOAT_EQ( LensFlareGhostWeight( glm::vec2( 0.5f, -0.2f ), centre, centre, 1.0f, kAspect ), 0.0f );
    EXPECT_GT( LensFlareGhostWeight( sun, centre, centre, 1.0f, kAspect ), 0.0f );
}

// --- Halo -------------------------------------------------------------------------------------------

TEST( LensFlareHalo, ReadsTheSunExactlyOnTheRing )
{
    // THE bug this pins. The first implementation read the source at sunUv + bearing*radius — the far
    // side of the ring, which is empty sky — and the halo simply never appeared. On the ring the source
    // point must BE the sun, which is what puts the sun's own image on the ring.
    const glm::vec2 sun( 0.4f, 0.45f );
    const float     radius = 0.18f;

    for ( float angle = 0.0f; angle < 6.2f; angle += 0.7f )
    {
        // A screen point exactly `radius` away in aspect-corrected uv, on this bearing.
        const glm::vec2 bearing( std::cos( angle ), std::sin( angle ) );
        const glm::vec2 onRing = sun + ( bearing * radius ) / glm::vec2( kAspect, 1.0f );
        ASSERT_NEAR( Dist( onRing, sun, kAspect ), radius, 1e-4f );

        const glm::vec2 source = LensFlareHaloSourceUv( onRing, sun, radius, kAspect );
        EXPECT_NEAR( source.x, sun.x, 1e-4f ) << "bearing " << angle;
        EXPECT_NEAR( source.y, sun.y, 1e-4f ) << "bearing " << angle;
    }
}

TEST( LensFlareHalo, RingWeightPeaksOnTheRingAndVanishesAtTheSun )
{
    const glm::vec2 sun( 0.4f, 0.45f );
    const float     radius = 0.18f;

    const glm::vec2 atSun  = sun;
    const glm::vec2 onRing = sun + glm::vec2( radius / kAspect, 0.0f );
    const glm::vec2 wayOut = sun + glm::vec2( ( radius * 2.5f ) / kAspect, 0.0f );

    const float centreW = LensFlareHaloWeight( atSun, sun, radius, kAspect );
    const float ringW   = LensFlareHaloWeight( onRing, sun, radius, kAspect );
    const float outW    = LensFlareHaloWeight( wayOut, sun, radius, kAspect );

    EXPECT_NEAR( ringW, 1.0f, 1e-4f );
    EXPECT_LT( centreW, 0.01f ); // the halo is a RING — it must not fill the sun
    EXPECT_FLOAT_EQ( outW, 0.0f );
}

TEST( LensFlareHalo, IsACircleAtAnyAspectRatio )
{
    // Measured in raw uv a ring would be an ellipse on a 16:9 screen. The aspect correction is the only
    // thing making it round, so it is asserted at two very different shapes.
    const glm::vec2 sun( 0.5f, 0.5f );
    const float     radius = 0.2f;

    for ( float aspect : { 1.0f, 16.0f / 9.0f, 21.0f / 9.0f } )
    {
        for ( float angle = 0.0f; angle < 6.2f; angle += 0.9f )
        {
            const glm::vec2 bearing( std::cos( angle ), std::sin( angle ) );
            const glm::vec2 onRing = sun + ( bearing * radius ) / glm::vec2( aspect, 1.0f );
            EXPECT_NEAR( LensFlareHaloWeight( onRing, sun, radius, aspect ), 1.0f, 1e-4f )
                 << "aspect " << aspect << " angle " << angle;
        }
    }
}

// --- Streak -----------------------------------------------------------------------------------------

TEST( LensFlareStreak, TapsSpanTheAxisSymmetricallyAndPeakAtThePixel )
{
    const float taps = 64.0f;

    EXPECT_FLOAT_EQ( LensFlareStreakTapOffset( 0.0f, taps ), -1.0f );
    EXPECT_FLOAT_EQ( LensFlareStreakTapOffset( taps - 1.0f, taps ), 1.0f );

    // Symmetric about the pixel, and the triangular kernel is heaviest there.
    const float middle = LensFlareStreakTapOffset( ( taps - 1.0f ) * 0.5f, taps );
    EXPECT_NEAR( middle, 0.0f, 1e-5f );
    EXPECT_NEAR( LensFlareStreakTapWeight( 0.0f ), 1.0f, 1e-6f );
    EXPECT_NEAR( LensFlareStreakTapWeight( 1.0f ), 0.0f, 1e-6f );
    EXPECT_NEAR( LensFlareStreakTapWeight( -0.5f ), LensFlareStreakTapWeight( 0.5f ), 1e-6f );
}

TEST( LensFlareStreak, ConsecutiveTapsOverlapTheSunDiscRatherThanStampingCopiesOfIt )
{
    // THE other bug this pins. The streak is a convolution of the source with its axis, and it only
    // reads as a line if consecutive taps land closer together than the source feature is wide. At 16
    // taps over a 0.35 half-length the step was 0.047 in uv against a sun about 0.02 wide, and the
    // streak rendered as a row of separate squares — 16 copies of the sun. This asserts the relation
    // that has to hold instead, at the tap count the shader actually uses.
    constexpr float kShaderTaps = 64.0f; // kStreakTaps in LensFlareFeatures.shader
    constexpr float kSunUvWidth = 0.02f; // the 0.545-degree disc at the shot field of view

    const glm::vec2 uv( 0.5f, 0.5f );
    const glm::vec2 axis( 1.0f, 0.0f );
    const float     length = 0.35f;

    float widest = 0.0f;
    for ( float k = 1.0f; k < kShaderTaps; k += 1.0f )
    {
        const glm::vec2 a =
             LensFlareStreakTapUv( uv, axis, LensFlareStreakTapOffset( k - 1.0f, kShaderTaps ), length, kAspect );
        const glm::vec2 b =
             LensFlareStreakTapUv( uv, axis, LensFlareStreakTapOffset( k, kShaderTaps ), length, kAspect );
        widest = std::max( widest, Dist( a, b, kAspect ) );
    }

    EXPECT_LT( widest, kSunUvWidth )
         << "streak taps are further apart than the sun is wide — the streak will render as separate "
            "copies of the disc rather than one smear";
}

TEST( LensFlareStreak, FollowsTheAuthoredAxisAndIsCircularlyMeasured )
{
    // A vertical streak must be vertical, and its extent must be the same length as a horizontal one —
    // the aspect correction applies to the axis too, or a 90-degree Streak Angle would be shorter.
    const glm::vec2 uv( 0.5f, 0.5f );
    const float     length = 0.3f;

    const glm::vec2 h = LensFlareStreakTapUv( uv, glm::vec2( 1.0f, 0.0f ), 1.0f, length, kAspect );
    const glm::vec2 v = LensFlareStreakTapUv( uv, glm::vec2( 0.0f, 1.0f ), 1.0f, length, kAspect );

    EXPECT_NEAR( h.y, uv.y, 1e-6f );
    EXPECT_NEAR( v.x, uv.x, 1e-6f );
    EXPECT_NEAR( Dist( h, uv, kAspect ), Dist( v, uv, kAspect ), 1e-5f );
}

// --- The gate ---------------------------------------------------------------------------------------

TEST( LensFlareStrengthRule, IsZeroForASunBehindTheCamera )
{
    // The single property the whole effect is judged on. ComputeSunScreen returns Fade 0 behind the
    // camera; LensFlareStrength must turn that into no flare at all, whatever the authored intensity.
    const glm::mat4 projection = glm::perspective( glm::radians( 60.0f ), 16.0f / 9.0f, 0.1f, 10000.0f );
    const glm::mat4 view =
         glm::lookAt( glm::vec3( 0.0f ), glm::vec3( 0.0f, 0.0f, -1.0f ), glm::vec3( 0.0f, 1.0f, 0.0f ) );

    const SunScreen behind = ComputeSunScreen( projection * view, glm::vec3( 0.0f, 0.0f, 1.0f ) );
    ASSERT_FLOAT_EQ( behind.Fade, 0.0f );
    EXPECT_FLOAT_EQ( LensFlareStrength( behind.Fade, 5.0f ), 0.0f );

    const SunScreen ahead = ComputeSunScreen( projection * view, glm::vec3( 0.0f, 0.0f, -1.0f ) );
    ASSERT_FLOAT_EQ( ahead.Fade, 1.0f );
    EXPECT_GT( LensFlareStrength( ahead.Fade, 0.35f ), 0.0f );
}

TEST( LensFlareStrengthRule, NeverSubtractsLightFromTheFrame )
{
    // The flare is added in HDR before the tonemap. A negative intensity, however it got there, must
    // dim the flare to nothing rather than darken the scene.
    EXPECT_FLOAT_EQ( LensFlareStrength( 1.0f, -2.0f ), 0.0f );
    EXPECT_FLOAT_EQ( LensFlareStrength( -1.0f, 2.0f ), 0.0f );
}

TEST( LensFlareStrengthRule, RisesWithTheSunsScreenFadeSoTheFlareLeavesWithTheSun )
{
    float previous = -1.0f;
    for ( float fade = 0.0f; fade <= 1.0f; fade += 0.25f )
    {
        const float strength = LensFlareStrength( fade, 0.35f );
        EXPECT_GT( strength, previous );
        previous = strength;
    }
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
