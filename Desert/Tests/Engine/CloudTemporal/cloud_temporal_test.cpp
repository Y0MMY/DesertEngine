// The volumetric clouds' temporal stage and bilateral upsample, tested without a GPU.
//
// Three things are under test and they are different in kind:
//
//   1. THE SHADER'S OWN MATHS — CloudTemporalReference.hpp compiles the very GLSL header the resolve, the
//      composite and the raymarch include, so every assertion below is about the code that runs on the
//      GPU rather than about a CPU paraphrase of it: the depth-guide packing, the bilateral weights, the
//      reprojection through the shell mid-surface, the neighbourhood clamp and the blend.
//
//   2. THE CPU-SIDE DECISIONS — Graphic::MakeCloudTemporalPush turns a pair of cameras into the three
//      rows the shader multiplies by, Graphic::CloudSelectCompositeSource is the ONLY interpretation of
//      TemporalMode in the engine, and Graphic::CloudScaledImageBytes is the memory figure CLD-34 asks to
//      be able to check.
//
//   3. THAT `TemporalMode = Off` IS REAL (CLD-32a constraint 2, CLD-96d). Off is not a branch inside the
//      resolve: the stage is not dispatched and the composite is pointed at the marched image. The last
//      section drives that decision the way the renderer drives it and compares the RAW BITS of what the
//      composite would read against the raw bits of what the march produced.

#include "CloudTemporalReference.hpp"

#include <Engine/Core/Projection.hpp>
#include <Engine/Graphic/Clouds/CloudPayload.hpp>

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace R = Desert::Tests::CloudTemporalRef;

using Desert::Graphic::CloudCompositeSource;
using Desert::Graphic::CloudScaledImageBytes;
using Desert::Graphic::CloudSelectCompositeSource;
using Desert::Graphic::CloudTemporalPush;
using Desert::Graphic::CloudTemporalUsesHistory;
using Desert::Graphic::MakeCloudTemporalPush;
using Desert::Graphic::PackCloudParams;

namespace
{
    // Earth-scale defaults, in the units each side of the boundary uses.
    constexpr float kPlanetRadiusWorld = 636000000.0f;                   // 6360 km in world units (cm)
    constexpr float kPlanetRadiusKm    = kPlanetRadiusWorld / 100000.0f; // 6360
    constexpr float kBottomKm          = 1.5f;
    constexpr float kThicknessKm       = 3.5f;

    // What an rgba8 UNORM store does to a channel: clamp, scale by 255, round to nearest, and hand back
    // n/255 on the read. The guide's packing is designed around this, so the test has to model it rather
    // than compare floats that never reach the image in that form.
    float QuantizeUnorm8( float value )
    {
        const float clamped = std::fmin( std::fmax( value, 0.0f ), 1.0f );
        return std::round( clamped * 255.0f ) / 255.0f;
    }

    glm::vec4 StoreGuide( float distanceWorldUnits )
    {
        const glm::vec4 encoded = R::CloudEncodeGuideDistance( distanceWorldUnits );
        return glm::vec4( QuantizeUnorm8( encoded.x ), QuantizeUnorm8( encoded.y ), QuantizeUnorm8( encoded.z ),
                          QuantizeUnorm8( encoded.w ) );
    }

    float RoundTripGuide( float distanceWorldUnits )
    {
        return R::CloudDecodeGuideDistance( StoreGuide( distanceWorldUnits ) );
    }

    R::CloudTemporalBox BoxOf( const glm::vec4& lo, const glm::vec4& hi, float clampScale )
    {
        return R::CloudNeighbourhoodBox( lo, hi, clampScale );
    }

    bool SameBits( const glm::vec4& a, const glm::vec4& b )
    {
        return std::memcmp( &a, &b, sizeof( glm::vec4 ) ) == 0;
    }

    // A camera the way the engine builds one: Core::MakePerspective (reversed-Z) for the projection,
    // glm::lookAt for the
    // view, and the world origin on the planet surface with +Y up. Kept in pieces rather than as a single
    // product because that is what the push builder needs — the eye translation has to come out of the
    // view before the matrix is inverted.
    struct TestCamera
    {
        glm::mat4 Projection;
        glm::mat4 View;
        glm::vec3 Position;

        glm::mat4 ViewProjection() const
        {
            return Projection * View;
        }
    };

    TestCamera MakeCamera( const glm::vec3& position, const glm::vec3& forward )
    {
        TestCamera camera;
        camera.Projection =
             Desert::Core::MakePerspective( glm::radians( 60.0f ), 16.0f / 9.0f, 10.0f, 20000000.0f );
        camera.View       = glm::lookAt( position, position + forward, glm::vec3( 0.0f, 1.0f, 0.0f ) );
        camera.Position   = position;
        return camera;
    }

    CloudTemporalPush PushFor( const TestCamera& current, const TestCamera& previous, bool historyValid )
    {
        return MakeCloudTemporalPush( current.Projection, current.View, previous.ViewProjection(),
                                      current.Position, historyValid, /*checkerboard=*/false,
                                      /*frameIndex=*/0u );
    }

    R::CloudReprojection Reproject( const glm::vec2& uv, const CloudTemporalPush& push )
    {
        const glm::vec3 cameraPositionKm =
             glm::vec3( push.CameraPosition.x, push.CameraPosition.y, push.CameraPosition.z ) / 100000.0f;

        return R::CloudReprojectThroughShell( uv, push.InverseViewProjection, cameraPositionKm, kPlanetRadiusKm,
                                              kBottomKm, kThicknessKm, push.PrevReprojectionRow0,
                                              push.PrevReprojectionRow1, push.PrevReprojectionRow3 );
    }

    // The reprojection written out in double from absolute coordinates: the ray, the mid-surface sphere
    // and the projection, with nothing shared with the header under test. It is an independent answer,
    // which is the only kind worth comparing against.
    bool ReprojectDouble( const glm::vec2& uv, const glm::mat4& inverseViewProjection,
                          const glm::dvec3& cameraPosition, const glm::mat4& previousViewProjection,
                          glm::dvec2& outUv )
    {
        const glm::dvec2 ndc( double( uv.x ) * 2.0 - 1.0, 1.0 - double( uv.y ) * 2.0 );

        // Device depth 1 is the NEAR plane and 0 the far one — the engine is reversed-Z
        // (Core/Projection.hpp), and this reference has to build the ray the same way the shader does or
        // `dir` comes out negated and every ray misses the shell.
        const glm::vec4  nearH = inverseViewProjection * glm::vec4( float( ndc.x ), float( ndc.y ), 1.0f, 1.0f );
        const glm::vec4  farH  = inverseViewProjection * glm::vec4( float( ndc.x ), float( ndc.y ), 0.0f, 1.0f );
        const glm::dvec3 nearP( nearH.x / nearH.w, nearH.y / nearH.w, nearH.z / nearH.w );
        const glm::dvec3 farP( farH.x / farH.w, farH.y / farH.w, farH.z / farH.w );
        const glm::dvec3 dir = glm::normalize( farP - nearP );

        // Kilometre space, absolute, textbook quadratic against the sphere centred at (0, -R, 0).
        const glm::dvec3 originKm = cameraPosition / 100000.0;
        const glm::dvec3 centre( 0.0, -double( kPlanetRadiusKm ), 0.0 );
        const double     radius = double( kPlanetRadiusKm ) + double( kBottomKm ) + 0.5 * double( kThicknessKm );
        const glm::dvec3 oc     = centre - originKm;
        const double     b      = glm::dot( dir, oc );
        const double     c      = glm::dot( oc, oc ) - radius * radius;
        const double     disc   = b * b - c;
        if ( disc < 0.0 )
            return false;

        const double s  = std::sqrt( disc );
        const double t0 = b - s;
        const double t1 = b + s;
        const double t  = t0 > 0.0 ? t0 : t1;
        if ( t <= 0.0 )
            return false;

        const glm::dvec3 world = cameraPosition + dir * ( t * 100000.0 );

        glm::dmat4 previous( 0.0 );
        for ( int column = 0; column < 4; ++column )
            for ( int row = 0; row < 4; ++row )
                previous[column][row] = double( previousViewProjection[column][row] );

        const glm::dvec4 clip = previous * glm::dvec4( world, 1.0 );
        if ( clip.w <= 0.0 )
            return false;

        outUv = glm::dvec2( ( clip.x / clip.w ) * 0.5 + 0.5, 0.5 - ( clip.y / clip.w ) * 0.5 );
        return true;
    }
} // namespace

// ---- CLD-32: the depth guide the bilateral upsample reads -------------------------------------------

TEST( CloudDepthGuide, RoundTripsEveryDistanceThatDecidesASilhouette )
{
    // 10 m to 150 km — from the nearest geometry a cloud can be occluded by to the far edge of the layer.
    // The bound is the encoding's own: 16 bits of d/(d+1km) is 0.02 % at 100 m and 0.16 % at 100 km, so
    // 0.5 % is a real ceiling with margin, not a number chosen to make the test pass.
    for ( float metres = 10.0f; metres <= 150000.0f; metres *= 1.35f )
    {
        const float distance = metres * 100.0f; // world units are centimetres
        const float restored = RoundTripGuide( distance );
        EXPECT_NEAR( restored / distance, 1.0f, 5e-3f ) << "at " << metres << " m";
    }
}

TEST( CloudDepthGuide, IsMonotonicSoATapIsNeverJudgedNearerThanACloserOne )
{
    float previous = -1.0f;
    for ( float metres = 1.0f; metres <= 200000.0f; metres *= 1.1f )
    {
        const float restored = RoundTripGuide( metres * 100.0f );
        EXPECT_GE( restored, previous );
        previous = restored;
    }
}

TEST( CloudDepthGuide, StaysFiniteAtZeroAndFarBeyondTheLayer )
{
    EXPECT_FLOAT_EQ( RoundTripGuide( 0.0f ), 0.0f );
    EXPECT_TRUE( std::isfinite( RoundTripGuide( -1.0f ) ) );
    EXPECT_TRUE( std::isfinite( RoundTripGuide( 1.0e12f ) ) );
    EXPECT_GT( RoundTripGuide( 1.0e12f ), 0.0f );

    // The saturated code word must not decode to an infinity: it is multiplied into a weight, and one inf
    // would take the whole pixel with it.
    EXPECT_TRUE( std::isfinite( R::CloudDecodeGuideDistance( glm::vec4( 1.0f, 1.0f, 0.0f, 0.0f ) ) ) );
}

// ---- CLD-32: the bilateral upsample ------------------------------------------------------------------

TEST( CloudBilateralUpsample, DegeneratesToPlainBilinearWhereTheGuideAgrees )
{
    // Open sky, a flat wall, and EVERY pixel at Resolution Scale = Full: all four taps report the same
    // distance, and the filter must then be exactly the bilinear filter it replaced.
    const float distance = 250000.0f;

    for ( float fx = 0.0f; fx <= 1.0f; fx += 0.125f )
    {
        for ( float fy = 0.0f; fy <= 1.0f; fy += 0.125f )
        {
            const R::CloudUpsampleWeights w = R::CloudBilateralUpsampleWeights(
                 glm::vec2( fx, fy ), distance, distance, distance, distance, R::CLOUD_GUIDE_RELATIVE_TOLERANCE );

            EXPECT_NEAR( w.W00, ( 1.0f - fx ) * ( 1.0f - fy ), 1e-6f );
            EXPECT_NEAR( w.W10, fx * ( 1.0f - fy ), 1e-6f );
            EXPECT_NEAR( w.W01, ( 1.0f - fx ) * fy, 1e-6f );
            EXPECT_NEAR( w.W11, fx * fy, 1e-6f );
        }
    }
}

TEST( CloudBilateralUpsample, TheWeightsAlwaysPartitionUnityAndAreNeverNegative )
{
    const float distances[] = { 1000.0f, 45000.0f, 250000.0f, 9000000.0f, 15000000.0f };

    for ( float fx = 0.0f; fx <= 1.0f; fx += 0.2f )
    {
        for ( float fy = 0.0f; fy <= 1.0f; fy += 0.2f )
        {
            for ( float d00 : distances )
            {
                for ( float d11 : distances )
                {
                    const R::CloudUpsampleWeights w = R::CloudBilateralUpsampleWeights(
                         glm::vec2( fx, fy ), d00, d11, d00, d11, R::CLOUD_GUIDE_RELATIVE_TOLERANCE );

                    EXPECT_GE( w.W00, 0.0f );
                    EXPECT_GE( w.W10, 0.0f );
                    EXPECT_GE( w.W01, 0.0f );
                    EXPECT_GE( w.W11, 0.0f );
                    EXPECT_NEAR( w.W00 + w.W10 + w.W01 + w.W11, 1.0f, 1e-5f );
                }
            }
        }
    }
}

TEST( CloudBilateralUpsample, ATapOnTheOtherSideOfASilhouetteIsSuppressed )
{
    // The pixel sits nearest to the (0,0) tap, which marched all the way to the sky; the (1,1) tap
    // stopped on a foreground object twenty times nearer. This is the halo the filter exists to remove:
    // with plain bilinear that near tap would contribute 4 %.
    const float sky    = 15000000.0f; // 150 km
    const float object = 750000.0f;   // 7.5 km

    const R::CloudUpsampleWeights bilateral = R::CloudBilateralUpsampleWeights(
         glm::vec2( 0.2f, 0.2f ), sky, sky, sky, object, R::CLOUD_GUIDE_RELATIVE_TOLERANCE );

    EXPECT_LT( bilateral.W11, 1e-3f );
    EXPECT_GT( bilateral.W00, 0.6f );

    // ...and the taps that DO agree keep their relative proportions, so the surviving filter is still the
    // bilinear one restricted to the surface the pixel belongs to.
    EXPECT_NEAR( bilateral.W10 / bilateral.W01, 1.0f, 1e-4f );
}

TEST( CloudBilateralUpsample, TheNearestTapIsNeverTheOneThatIsRejected )
{
    // Whichever corner the pixel sits closest to becomes the reference, so its own similarity is exactly
    // 1 and it cannot be argued away by its neighbours — the property that keeps the sum away from zero
    // and makes the degenerate branch unnecessary.
    // Not named `near`/`far`: <windows.h> still #defines both to nothing (16-bit segment qualifiers),
    // gtest reaches that header on Windows, and `const float near = ...` compiles to `const float = ...`.
    const float nearDistance  = 100000.0f;
    const float farDistance   = 15000000.0f;
    const float corners[4][2] = { { 0.1f, 0.1f }, { 0.9f, 0.1f }, { 0.1f, 0.9f }, { 0.9f, 0.9f } };

    for ( int corner = 0; corner < 4; ++corner )
    {
        float d[4] = { farDistance, farDistance, farDistance, farDistance };
        d[corner]  = nearDistance;

        const R::CloudUpsampleWeights w =
             R::CloudBilateralUpsampleWeights( glm::vec2( corners[corner][0], corners[corner][1] ), d[0], d[1],
                                               d[2], d[3], R::CLOUD_GUIDE_RELATIVE_TOLERANCE );

        const float weights[4] = { w.W00, w.W10, w.W01, w.W11 };
        EXPECT_GT( weights[corner], 0.9f ) << "corner " << corner;
    }
}

TEST( CloudBilateralUpsample, AtFullResolutionThePixelIsItsOwnTexelWhateverTheGuideSays )
{
    // Resolution Scale = Full puts every fragment exactly on a texel centre, so the fraction is zero and
    // the magnification must be an identity — no smoothing, no cost, and no dependence on the guide.
    const R::CloudUpsampleWeights w = R::CloudBilateralUpsampleWeights(
         glm::vec2( 0.0f, 0.0f ), 100.0f, 9000000.0f, 42.0f, 15000000.0f, R::CLOUD_GUIDE_RELATIVE_TOLERANCE );

    EXPECT_FLOAT_EQ( w.W00, 1.0f );
    EXPECT_FLOAT_EQ( w.W10, 0.0f );
    EXPECT_FLOAT_EQ( w.W01, 0.0f );
    EXPECT_FLOAT_EQ( w.W11, 0.0f );
}

TEST( CloudBilateralUpsample, ALargerToleranceKeepsMoreOfTheDisagreeingTap )
{
    const float sky    = 15000000.0f;
    const float object = 750000.0f;

    float previous = -1.0f;
    for ( float tolerance = 0.01f; tolerance <= 100.0f; tolerance *= 3.0f )
    {
        const R::CloudUpsampleWeights w =
             R::CloudBilateralUpsampleWeights( glm::vec2( 0.5f, 0.5f ), sky, sky, sky, object, tolerance );
        EXPECT_GE( w.W11, previous );
        previous = w.W11;
    }
    // At a tolerance large enough to accept anything the filter is bilinear again: 0.25 per tap.
    EXPECT_NEAR( previous, 0.25f, 1e-3f );
}

// ---- CLD-32: reprojection through the shell ----------------------------------------------------------

TEST( CloudReprojection, AnIdenticalCameraReprojectsEveryPixelOntoItself )
{
    const TestCamera        camera = MakeCamera( glm::vec3( 0.0f, 200000.0f, 0.0f ), // 2 km up
                                                 glm::vec3( 0.0f, 0.05f, 1.0f ) );
    const CloudTemporalPush push   = PushFor( camera, camera, /*historyValid=*/true );

    for ( float u = 0.05f; u < 1.0f; u += 0.1f )
    {
        for ( float v = 0.05f; v < 1.0f; v += 0.1f )
        {
            const R::CloudReprojection result = Reproject( glm::vec2( u, v ), push );
            ASSERT_TRUE( result.Valid ) << "at (" << u << ", " << v << ")";
            EXPECT_NEAR( result.Uv.x, u, 1e-5f );
            EXPECT_NEAR( result.Uv.y, v, 1e-5f );
        }
    }
}

TEST( CloudReprojection, ACameraFarFromTheWorldOriginStillReprojectsOntoItself )
{
    // The case that decided how the push constant is built: the reconstruction has to resolve a
    // near-plane offset of tens of units out of camera coordinates of millions, and in single precision
    // that used to tilt every ray by an angle that grew with distance from the world origin. The builder
    // removes the eye translation before inverting, so the magnitude that causes it never enters.
    const glm::vec3  position( 3000000.0f, 200000.0f, -2000000.0f ); // 30 km east, 20 km south, 2 km up
    const TestCamera camera = MakeCamera( position, glm::vec3( 0.0f, 0.05f, 1.0f ) );

    const CloudTemporalPush push = PushFor( camera, camera, /*historyValid=*/true );

    // What the builder produces: a pixel reprojects onto itself, sub-thousandth of a screen.
    const R::CloudReprojection resolved = Reproject( glm::vec2( 0.5f, 0.05f ), push );
    ASSERT_TRUE( resolved.Valid );
    EXPECT_NEAR( resolved.Uv.x, 0.5f, 1e-4f );
    EXPECT_NEAR( resolved.Uv.y, 0.05f, 1e-4f );

    // AND THE NAIVE FORM NOW AGREES WITH IT, which it did NOT before the engine went reversed-Z. This
    // assertion used to be its opposite — that inverting the absolute view-projection moved the pixel by
    // ten lines of a 1080-line screen — and reversed-Z deleted the error rather than the test.
    //
    // WHY, because it is not luck. The projection's z row is scaled by P[2][2], which is -far/(far-near)
    // under standard-Z (essentially -1) and near/(far-near) under reversed-Z (here 5e-7). Under
    // standard-Z that row therefore carried the view's translation at full size, so rows 2 and 3 of the
    // view-projection both ended with about -3e6 in their fourth column; a cofactor inverse then
    // subtracts two ~9e12 products that nearly cancel, and the surviving digits are noise. Reversed-Z
    // leaves that entry at about 11 instead, the two rows stop being nearly parallel, and the inverse is
    // well conditioned. Same camera, same distance, same single precision.
    //
    // The builder still removes the eye translation, and should: that is unconditional, whereas the
    // conditioning above is a consequence of one particular near/far ratio. This asserts the two forms
    // now agree — if they ever diverge again, the eye-relative construction is what is holding the frame
    // together and this test says so.
    CloudTemporalPush naive         = push;
    naive.InverseViewProjection     = glm::inverse( camera.ViewProjection() );
    const R::CloudReprojection also = Reproject( glm::vec2( 0.5f, 0.05f ), naive );
    ASSERT_TRUE( also.Valid );
    EXPECT_NEAR( also.Uv.y, 0.05f, 1e-4f );
}

TEST( CloudReprojection, APureTranslationMatchesADoublePrecisionReference )
{
    const glm::vec3 forward( 0.0f, 0.05f, 1.0f );

    const TestCamera previous = MakeCamera( glm::vec3( 0.0f, 200000.0f, 0.0f ), forward );
    // 3.5 km east, 1.2 km north of it
    const TestCamera current = MakeCamera( glm::vec3( 350000.0f, 205000.0f, 120000.0f ), forward );

    const glm::mat4         previousViewProjection = previous.ViewProjection();
    const CloudTemporalPush push                   = PushFor( current, previous, /*historyValid=*/true );

    int compared = 0;
    for ( float u = 0.1f; u < 1.0f; u += 0.2f )
    {
        for ( float v = 0.1f; v < 1.0f; v += 0.2f )
        {
            glm::dvec2 reference( 0.0 );
            ASSERT_TRUE( ReprojectDouble( glm::vec2( u, v ), push.InverseViewProjection,
                                          glm::dvec3( current.Position ), previousViewProjection, reference ) );

            const R::CloudReprojection result = Reproject( glm::vec2( u, v ), push );

            // The camera MOVED, so the answer must not be the input — otherwise this test would pass on a
            // function that ignored the previous camera entirely.
            EXPECT_GT( std::abs( double( result.Uv.x ) - double( u ) ) +
                            std::abs( double( result.Uv.y ) - double( v ) ),
                       1e-4 );

            // 1e-3 of a screen: the shell intersection is a single-precision expanded quadratic against a
            // 6360 km sphere, and CloudMath already pins that to 0.1 % of the entry distance. A tighter
            // bound here would be pinning float rounding, not the reprojection.
            EXPECT_NEAR( double( result.Uv.x ), reference.x, 1e-3 );
            EXPECT_NEAR( double( result.Uv.y ), reference.y, 1e-3 );
            ++compared;
        }
    }
    EXPECT_EQ( compared, 25 );
}

TEST( CloudReprojection, APixelThatLeavesTheScreenIsRejected )
{
    // A whip pan: the same camera position, 70 degrees of yaw between the frames. The pixels on the
    // leading edge of the turn were not on the previous screen at all, and the stage must say so rather
    // than clamp them onto the border and drag a stretched edge across the sky.
    const glm::vec3  position( 0.0f, 200000.0f, 0.0f );
    const TestCamera previous = MakeCamera( position, glm::vec3( 0.0f, 0.0f, 1.0f ) );
    const TestCamera current  = MakeCamera(
         position, glm::vec3( std::sin( glm::radians( 70.0f ) ), 0.0f, std::cos( glm::radians( 70.0f ) ) ) );

    const CloudTemporalPush push = PushFor( current, previous, /*historyValid=*/true );

    int rejected          = 0;
    int accepted          = 0;
    int rejectedOffScreen = 0;
    for ( float u = 0.05f; u < 1.0f; u += 0.05f )
    {
        const R::CloudReprojection result = Reproject( glm::vec2( u, 0.5f ), push );
        if ( result.Valid )
        {
            ++accepted;
            continue;
        }
        ++rejected;

        // Counted separately, because there are two ways to be rejected and only one of them is the
        // screen-bounds test: a point BEHIND the previous camera never gets a coordinate at all. Without
        // this distinction the test would still pass on a function that accepted every on-screen
        // coordinate it was given, however far outside the screen it landed.
        if ( result.Uv.x < 0.0f || result.Uv.x > 1.0f || result.Uv.y < 0.0f || result.Uv.y > 1.0f )
            ++rejectedOffScreen;
    }

    EXPECT_GT( rejected, 0 );
    EXPECT_GT( rejectedOffScreen, 0 );
    EXPECT_GT( accepted, 0 ); // ...and not everything: a 70 degree turn still shares part of its view
}

TEST( CloudReprojection, APointBehindThePreviousCameraIsRejected )
{
    // Turned round completely. Every reprojected point now has a negative clip w, which is the case that
    // would otherwise FOLD back onto the screen as a plausible-looking coordinate and sample history from
    // the wrong half of the sky.
    const glm::vec3  position( 0.0f, 200000.0f, 0.0f );
    const TestCamera previous = MakeCamera( position, glm::vec3( 0.0f, 0.0f, 1.0f ) );
    const TestCamera current  = MakeCamera( position, glm::vec3( 0.0f, 0.0f, -1.0f ) );

    const CloudTemporalPush push = PushFor( current, previous, /*historyValid=*/true );

    for ( float u = 0.2f; u < 1.0f; u += 0.2f )
        EXPECT_FALSE( Reproject( glm::vec2( u, 0.5f ), push ).Valid ) << "at u = " << u;
}

TEST( CloudReprojection, ARayThatNeverMeetsTheLayerHasNoHistory )
{
    // Above the layer, looking up and away. There is no cloud along this ray, so there is nothing to
    // carry over, and the stage says Valid = false instead of inventing an intersection behind itself.
    // 10 km up, above a layer that tops out at 5 km
    const TestCamera camera = MakeCamera( glm::vec3( 0.0f, 1000000.0f, 0.0f ), glm::vec3( 0.0f, 1.0f, 0.05f ) );
    const CloudTemporalPush push = PushFor( camera, camera, /*historyValid=*/true );

    EXPECT_FALSE( Reproject( glm::vec2( 0.5f, 0.5f ), push ).Valid );
}

TEST( CloudReprojection, TheSameCameraFromBelowTheLayerStillReprojectsOntoItself )
{
    // From inside the mid-sphere every direction has a forward exit, which is the branch the "first root
    // ahead of the camera" rule exists for. On the ground looking up is the common case.
    // 170 m up, well below the 1.5 km base
    const TestCamera camera      = MakeCamera( glm::vec3( 0.0f, 17000.0f, 0.0f ), glm::vec3( 0.0f, 0.4f, 1.0f ) );
    const CloudTemporalPush push = PushFor( camera, camera, /*historyValid=*/true );

    const R::CloudReprojection result = Reproject( glm::vec2( 0.5f, 0.4f ), push );
    ASSERT_TRUE( result.Valid );
    EXPECT_NEAR( result.Uv.x, 0.5f, 1e-5f );
    EXPECT_NEAR( result.Uv.y, 0.4f, 1e-5f );
}

// ---- CLD-32: the neighbourhood clamp -----------------------------------------------------------------

TEST( CloudNeighbourhoodClamp, AScaleOfOneIsExactlyTheMinMaxBox )
{
    const glm::vec4 lo( 0.1f, 0.2f, 0.3f, 0.4f );
    const glm::vec4 hi( 0.9f, 0.8f, 0.7f, 0.6f );

    const R::CloudTemporalBox box = BoxOf( lo, hi, 1.0f );
    for ( int c = 0; c < 4; ++c )
    {
        EXPECT_NEAR( box.Lo[c], lo[c], 1e-6f );
        EXPECT_NEAR( box.Hi[c], hi[c], 1e-6f );
    }
}

TEST( CloudNeighbourhoodClamp, ASampleInsideTheBoxPassesThroughUntouched )
{
    const R::CloudTemporalBox box = BoxOf( glm::vec4( 0.0f ), glm::vec4( 1.0f ), 1.0f );
    const glm::vec4           history( 0.25f, 0.5f, 0.75f, 1.0f );

    EXPECT_TRUE( SameBits( R::CloudClampToNeighbourhood( history, box ), history ) );
}

TEST( CloudNeighbourhoodClamp, ASampleOutsideIsPulledOntoTheBoundaryAndNotDiscarded )
{
    const R::CloudTemporalBox box = BoxOf( glm::vec4( 0.2f ), glm::vec4( 0.8f ), 1.0f );
    const glm::vec4 clamped       = R::CloudClampToNeighbourhood( glm::vec4( -5.0f, 5.0f, 0.5f, 100.0f ), box );

    EXPECT_FLOAT_EQ( clamped.x, 0.2f );
    EXPECT_FLOAT_EQ( clamped.y, 0.8f );
    EXPECT_FLOAT_EQ( clamped.z, 0.5f );
    EXPECT_FLOAT_EQ( clamped.w, 0.8f );
}

TEST( CloudNeighbourhoodClamp, TheClampScaleWidensTheBoxMonotonically )
{
    const glm::vec4 lo( 0.3f );
    const glm::vec4 hi( 0.7f );

    float previousWidth = -1.0f;
    for ( float scale = 0.5f; scale <= 4.0f; scale += 0.25f ) // the component's authorable range
    {
        const R::CloudTemporalBox box = BoxOf( lo, hi, scale );

        // The centre never moves — widening is symmetric, so the knob cannot brighten or darken the sky
        // as a side effect of loosening it.
        EXPECT_NEAR( 0.5f * ( box.Lo.x + box.Hi.x ), 0.5f, 1e-6f );

        const float width = box.Hi.x - box.Lo.x;
        EXPECT_GT( width, previousWidth );
        previousWidth = width;
    }
}

TEST( CloudNeighbourhoodClamp, AWiderBoxAcceptsEverythingANarrowerOneDid )
{
    // The monotonicity that makes the tooltip true: raising Temporal Clamp Scale can only ever keep MORE
    // of the history, never less.
    const glm::vec4 lo( 0.4f );
    const glm::vec4 hi( 0.6f );
    const glm::vec4 history( 0.9f, 0.1f, 0.5f, 0.62f );

    float previousDistanceToHistory = std::numeric_limits<float>::max();
    for ( float scale = 0.5f; scale <= 4.0f; scale += 0.5f )
    {
        const glm::vec4 clamped  = R::CloudClampToNeighbourhood( history, BoxOf( lo, hi, scale ) );
        const float     distance = glm::length( clamped - history );
        EXPECT_LE( distance, previousDistanceToHistory + 1e-6f );
        previousDistanceToHistory = distance;
    }
}

// ---- CLD-32 / CLD-96d: the blend, and what makes `Off` real ------------------------------------------

TEST( CloudTemporalBlend, TheAuthoredWeightIsTheShareOfTheCurrentFrame )
{
    const R::CloudTemporalBox box = BoxOf( glm::vec4( 0.0f ), glm::vec4( 10.0f ), 1.0f );
    const glm::vec4           current( 1.0f, 2.0f, 3.0f, 0.5f );
    const glm::vec4           history( 5.0f, 6.0f, 7.0f, 0.9f );

    const glm::vec4 resolved = R::CloudTemporalResolve( current, current, history, true, 0.25f, box );

    for ( int c = 0; c < 4; ++c )
        EXPECT_NEAR( resolved[c], current[c] * 0.25f + history[c] * 0.75f, 1e-6f );
}

TEST( CloudTemporalBlend, ALowerWeightKeepsMoreOfTheHistory )
{
    const R::CloudTemporalBox box = BoxOf( glm::vec4( 0.0f ), glm::vec4( 10.0f ), 1.0f );
    const glm::vec4           current( 1.0f );
    const glm::vec4           history( 9.0f );

    float previous = 0.0f;
    for ( float weight = 0.9f; weight >= 0.05f; weight -= 0.1f )
    {
        const glm::vec4 resolved = R::CloudTemporalResolve( current, current, history, true, weight, box );
        EXPECT_GT( resolved.x, previous );
        previous = resolved.x;
    }
}

TEST( CloudTemporalBlend, TheHistoryIsClampedBeforeItIsBlendedIn )
{
    // The point of the clamp: a history value the current neighbourhood cannot vouch for contributes its
    // BOUNDARY, not itself. Without this the ghost of a cloud that has drifted away keeps its full
    // brightness for as long as the accumulation runs.
    const R::CloudTemporalBox box = BoxOf( glm::vec4( 0.0f ), glm::vec4( 1.0f ), 1.0f );
    const glm::vec4           current( 0.5f );
    const glm::vec4           wildHistory( 40.0f );

    const glm::vec4 resolved = R::CloudTemporalResolve( current, current, wildHistory, true, 0.1f, box );
    EXPECT_NEAR( resolved.x, 0.5f * 0.1f + 1.0f * 0.9f, 1e-6f );
}

TEST( CloudTemporalOff, AWeightOfOneReturnsTheCurrentFrameBitForBit )
{
    // The Low tier authors Temporal Blend Factor = 1 alongside Temporal Mode = Off. Even reached through
    // the resolve — as it would be if the mode were ever forced on at that tier — the answer has to be
    // the marched frame itself and not a blend that happens to round to it.
    const R::CloudTemporalBox box = BoxOf( glm::vec4( 0.0f ), glm::vec4( 1.0f ), 1.0f );

    const glm::vec4 samples[] = { glm::vec4( 0.0f, 0.0f, 0.0f, 1.0f ),
                                  glm::vec4( 1.2345678f, 1e-8f, 6.02e23f, 0.5f ),
                                  glm::vec4( -3.5f, 1e-30f, 12345.678f, 0.0f ) };

    const glm::vec4 hostileHistories[] = {
         glm::vec4( 1e30f ),
         glm::vec4( std::numeric_limits<float>::infinity() ),
         glm::vec4( std::numeric_limits<float>::quiet_NaN() ),
    };

    for ( const glm::vec4& current : samples )
        for ( const glm::vec4& history : hostileHistories )
            EXPECT_TRUE(
                 SameBits( R::CloudTemporalResolve( current, current, history, true, 1.0f, box ), current ) );
}

TEST( CloudTemporalOff, HistoryThatIsNotUsableResolvesToTheNeighbourhoodMean )
{
    // Two ways to get here and both are ordinary: the first frame after the images are allocated (the
    // memory holds whatever the allocator left in it), and any pixel whose reprojection left the screen.
    // The second happens EVERY frame the camera turns, along the edge it turns toward, which is why the
    // answer is the 3x3 mean and not this pixel's own jittered texel — one sample of a dithered
    // half-resolution march beside an interior that averages ten frames is the seam that crawls.
    const R::CloudTemporalBox box = BoxOf( glm::vec4( 0.0f ), glm::vec4( 1.0f ), 1.0f );
    const glm::vec4           current( 0.125f, 7.5f, 1e-12f, 0.75f );
    const glm::vec4           mean( 0.2f, 6.0f, 0.5f, 0.6f );

    const glm::vec4 garbage( std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                             -std::numeric_limits<float>::infinity(), 1e38f );

    // Every weight below 1 — and the garbage history reaches the output through no path at all.
    for ( float weight : { 0.02f, 0.1f, 0.5f } )
        EXPECT_TRUE( SameBits( R::CloudTemporalResolve( current, mean, garbage, false, weight, box ), mean ) );

    // A weight of exactly 1 is the authored "no accumulation" and still answers with the marched frame
    // itself, history or no history.
    EXPECT_TRUE( SameBits( R::CloudTemporalResolve( current, mean, garbage, false, 1.0f, box ), current ) );
}

TEST( CloudTemporalOff, AFlatNeighbourhoodWithNoHistoryIsStillTheCurrentFrameBitForBit )
{
    // Where the mean has nothing to average — open sky, a cloud interior, every pixel of a still frame at
    // Resolution Scale = Full — the fallback degenerates to the texel it replaced. That degeneration is
    // what keeps the change confined to the band it was written for.
    const R::CloudTemporalBox box = BoxOf( glm::vec4( 0.0f ), glm::vec4( 1.0f ), 1.0f );

    const glm::vec4 samples[] = { glm::vec4( 0.0f, 0.0f, 0.0f, 1.0f ),
                                  glm::vec4( 1.2345678f, 1e-8f, 6.02e23f, 0.5f ),
                                  glm::vec4( -3.5f, 1e-30f, 12345.678f, 0.0f ) };

    const glm::vec4 garbage( std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                             -std::numeric_limits<float>::infinity(), 1e38f );

    for ( const glm::vec4& current : samples )
        for ( float weight : { 0.02f, 0.1f, 0.5f } )
            EXPECT_TRUE(
                 SameBits( R::CloudTemporalResolve( current, current, garbage, false, weight, box ), current ) );
}

// ---- CLD-32a / CLD-96d: `Off` is a configuration, not a failure branch -------------------------------

namespace
{
    // The renderer's decision, reproduced exactly: which image the composite magnifies, and — when the
    // resolve runs — what that image contains. Everything here is the code the renderer calls
    // (CloudSelectCompositeSource, CloudTemporalResolve); only the loop over texels is written out.
    struct SimulatedFrame
    {
        CloudCompositeSource   Source;
        std::vector<glm::vec4> Composited; // what the composite would sample, texel for texel
    };

    SimulatedFrame RunCloudStages( Desert::ECS::CloudTemporalMode mode, bool historyAllocates,
                                   const std::vector<glm::vec4>& marched, const std::vector<glm::vec4>& history,
                                   float blendFactor, float clampScale )
    {
        SimulatedFrame frame;
        frame.Source = CloudSelectCompositeSource( mode, historyAllocates );

        if ( frame.Source == CloudCompositeSource::Raymarch )
        {
            // No dispatch at all. The composite reads the raymarch target itself — the same memory the
            // march wrote, so this is an alias and not a copy.
            frame.Composited = marched;
            return frame;
        }

        frame.Composited.resize( marched.size() );
        for ( size_t i = 0; i < marched.size(); ++i )
        {
            // A one-texel neighbourhood is the honest reduction of the 3x3 for a flat test image, and it
            // is the tightest box the clamp can be given.
            const R::CloudTemporalBox box = BoxOf( marched[i], marched[i], clampScale );
            frame.Composited[i] =
                 R::CloudTemporalResolve( marched[i], marched[i], history[i], true, blendFactor, box );
        }
        return frame;
    }

    std::vector<glm::vec4> MarchedTestImage()
    {
        std::vector<glm::vec4> image;
        for ( int i = 0; i < 64; ++i )
        {
            const float t = float( i );
            image.push_back( glm::vec4( t * 0.011f, t * 1e-4f, 6.02e5f / ( t + 1.0f ), 1.0f - t / 64.0f ) );
        }
        return image;
    }
} // namespace

TEST( CloudTemporalMode, OffCompositesTheMarchedImageItselfBitForBit )
{
    const std::vector<glm::vec4> marched = MarchedTestImage();
    const std::vector<glm::vec4> history( marched.size(), glm::vec4( 999.0f ) );

    const SimulatedFrame frame = RunCloudStages( Desert::ECS::CloudTemporalMode::Off,
                                                 /*historyAllocates=*/true, marched, history, 0.1f, 1.5f );

    EXPECT_EQ( frame.Source, CloudCompositeSource::Raymarch );
    ASSERT_EQ( frame.Composited.size(), marched.size() );
    EXPECT_EQ( std::memcmp( frame.Composited.data(), marched.data(), marched.size() * sizeof( glm::vec4 ) ), 0 );
}

TEST( CloudTemporalMode, ReprojectionCompositesSomethingElse )
{
    // The counterpart the bit-for-bit test needs: if the resolve produced the marched image too, the test
    // above would be passing on a stage that does nothing.
    const std::vector<glm::vec4> marched = MarchedTestImage();
    const std::vector<glm::vec4> history( marched.size(), glm::vec4( 0.5f, 0.5f, 0.5f, 0.5f ) );

    const SimulatedFrame frame = RunCloudStages( Desert::ECS::CloudTemporalMode::Reprojection,
                                                 /*historyAllocates=*/true, marched, history, 0.1f, 1.5f );

    EXPECT_EQ( frame.Source, CloudCompositeSource::TemporalHistory );
    EXPECT_NE( std::memcmp( frame.Composited.data(), marched.data(), marched.size() * sizeof( glm::vec4 ) ), 0 );
}

TEST( CloudTemporalMode, HistoryThatCouldNotBeAllocatedFallsBackToTheMarchedImage )
{
    // CLD-34 requires an allocation failure to be latched and survivable. Survivable means exactly this:
    // the configuration degrades to the one Temporal Mode = Off already describes, rather than pointing
    // the composite at an image that was never created.
    const std::vector<glm::vec4> marched = MarchedTestImage();
    const std::vector<glm::vec4> history( marched.size(), glm::vec4( 0.5f ) );

    const SimulatedFrame frame = RunCloudStages( Desert::ECS::CloudTemporalMode::Reprojection,
                                                 /*historyAllocates=*/false, marched, history, 0.1f, 1.5f );

    EXPECT_EQ( frame.Source, CloudCompositeSource::Raymarch );
    EXPECT_EQ( std::memcmp( frame.Composited.data(), marched.data(), marched.size() * sizeof( glm::vec4 ) ), 0 );
}

TEST( CloudTemporalMode, TheModeHasExactlyOneInterpretation )
{
    EXPECT_FALSE( CloudTemporalUsesHistory( Desert::ECS::CloudTemporalMode::Off ) );
    EXPECT_TRUE( CloudTemporalUsesHistory( Desert::ECS::CloudTemporalMode::Reprojection ) );
}

// ---- The checkerboard: half the pixels marched per frame, the resolve reconstructs the rest ---------

TEST( CloudCheckerboard, ExactlyHalfThePixelsAreFreshEachFrame )
{
    // The whole point of the scheme is that the march does half the work per frame. If the pattern ever
    // drifted from an exact half — an off-by-one in the parity — the saving would drift with it and the
    // reconstruction would starve one diagonal of the screen.
    for ( uint32_t frame : { 0u, 1u, 2u, 63u } )
    {
        int freshCount = 0;
        for ( int y = 0; y < 16; ++y )
            for ( int x = 0; x < 16; ++x )
                if ( R::CloudCheckerboardFresh( glm::ivec2( x, y ), frame ) )
                    ++freshCount;
        EXPECT_EQ( freshCount, 16 * 16 / 2 );
    }
}

TEST( CloudCheckerboard, EveryPixelIsFreshExactlyOncePerTwoFrames )
{
    // The reconstruction's staleness bound: a pixel the march skips this frame MUST be marched the next,
    // or some pixel would coast on reprojected history forever.
    for ( int y = 0; y < 8; ++y )
        for ( int x = 0; x < 8; ++x )
            for ( uint32_t frame = 0; frame < 4; ++frame )
            {
                const bool now  = R::CloudCheckerboardFresh( glm::ivec2( x, y ), frame );
                const bool next = R::CloudCheckerboardFresh( glm::ivec2( x, y ), frame + 1 );
                EXPECT_NE( now, next );
            }
}

TEST( CloudCheckerboard, AdjacentPixelsAreNeverBothStale )
{
    // What makes the fresh-neighbour mean a real fallback: every stale pixel's 4-neighbourhood is
    // entirely fresh, so a pixel with no usable history always has same-frame data touching it.
    for ( int y = 0; y < 8; ++y )
        for ( int x = 0; x < 8; ++x )
            for ( uint32_t frame : { 0u, 1u } )
            {
                const bool here  = R::CloudCheckerboardFresh( glm::ivec2( x, y ), frame );
                const bool right = R::CloudCheckerboardFresh( glm::ivec2( x + 1, y ), frame );
                const bool below = R::CloudCheckerboardFresh( glm::ivec2( x, y + 1 ), frame );
                EXPECT_NE( here, right );
                EXPECT_NE( here, below );
            }
}

TEST( CloudCheckerboard, AWeightOfZeroReturnsTheClampedHistoryAndNeverTouchesTheCurrentTexel )
{
    // A stale pixel resolves at weight 0, and its `current` is the one texel the march deliberately did
    // not write this frame — the last place uninitialised image memory can still be standing. The
    // symmetric twin of "a weight of one returns the current frame bit for bit": current * 0 is a NaN
    // the moment that texel holds an inf, so the resolve must return the clamped history WITHOUT the
    // multiply.
    const R::CloudTemporalBox box = BoxOf( glm::vec4( 0.0f ), glm::vec4( 1.0f ), 1.0f );

    const glm::vec4 hostileCurrents[] = {
         glm::vec4( std::numeric_limits<float>::infinity() ),
         glm::vec4( std::numeric_limits<float>::quiet_NaN() ),
         glm::vec4( 1e38f ),
    };

    const glm::vec4 history( 0.25f, 0.5f, 0.75f, 1.0f ); // inside the box: the clamp is the identity

    for ( const glm::vec4& current : hostileCurrents )
        EXPECT_TRUE( SameBits( R::CloudTemporalResolve( current, current, history, true, 0.0f, box ), history ) );

    // Outside the box the answer is the BOUNDARY, exactly as any other clamped history.
    const glm::vec4 wild( 40.0f );
    EXPECT_TRUE(
         SameBits( R::CloudTemporalResolve( hostileCurrents[0], hostileCurrents[0], wild, true, 0.0f, box ),
                   glm::vec4( 1.0f ) ) );
}

TEST( CloudCheckerboard, RunsOnlyAtFullResolutionWithAUsableHistory )
{
    using Desert::ECS::CloudResolutionScale;
    using Desert::ECS::CloudTemporalMode;
    using Desert::Graphic::CloudCheckerboardActive;

    // The one configuration that checkerboards: Full resolution, a temporal mode that accumulates, and
    // a history that actually allocated. Everything else marches every pixel — in particular a Custom
    // Full + Temporal Off, where a checkerboarded march would put a half-stale checkerboard on screen
    // with no stage to repair it.
    EXPECT_TRUE( CloudCheckerboardActive( CloudResolutionScale::Full, CloudTemporalMode::Reprojection, true ) );

    EXPECT_FALSE( CloudCheckerboardActive( CloudResolutionScale::Full, CloudTemporalMode::Reprojection, false ) );
    EXPECT_FALSE( CloudCheckerboardActive( CloudResolutionScale::Full, CloudTemporalMode::Off, true ) );
    EXPECT_FALSE( CloudCheckerboardActive( CloudResolutionScale::Half, CloudTemporalMode::Reprojection, true ) );
    EXPECT_FALSE(
         CloudCheckerboardActive( CloudResolutionScale::Quarter, CloudTemporalMode::Reprojection, true ) );
}

// ---- CLD-35: the push constant, and CLD-34: the memory --------------------------------------------

TEST( CloudTemporalPushConstant, FillsTheGuaranteedRangeExactly )
{
    EXPECT_EQ( sizeof( CloudTemporalPush ), 128u );
    EXPECT_LE( sizeof( CloudTemporalPush ), 128u );
}

TEST( CloudTemporalPushConstant, TheHistoryFlagIsCarriedInTheCameraPositionW )
{
    const glm::vec3  position( 12.0f, 34.0f, 56.0f );
    const TestCamera camera = MakeCamera( position, glm::vec3( 0.0f, 0.0f, 1.0f ) );

    EXPECT_FLOAT_EQ( PushFor( camera, camera, false ).CameraPosition.w, 0.0f );
    EXPECT_FLOAT_EQ( PushFor( camera, camera, true ).CameraPosition.w, 1.0f );

    const CloudTemporalPush push = PushFor( camera, camera, true );
    EXPECT_FLOAT_EQ( push.CameraPosition.x, position.x );
    EXPECT_FLOAT_EQ( push.CameraPosition.y, position.y );
    EXPECT_FLOAT_EQ( push.CameraPosition.z, position.z );
}

TEST( CloudTemporalPushConstant, TheFlagWordRoundTripsThroughTheShaderDecoderForEveryCombination )
{
    // CloudTemporalPackFlags (C++) and CloudTemporalDecodeFlags (the GLSL the resolve compiles) are the
    // two halves of one encoding; if they ever disagreed, the resolve would blend stale data as fresh or
    // read a history that is not there — with no validation error anywhere. Frame indices beyond the
    // parity must land on their parity's representative: the pattern has period two and packing more
    // would spend bits the 128-byte budget does not have.
    using Desert::Graphic::CloudTemporalPackFlags;

    for ( bool history : { false, true } )
        for ( bool checkerboard : { false, true } )
            for ( uint32_t frame : { 0u, 1u, 2u, 3u, 62u, 63u } )
            {
                const float                 packed = CloudTemporalPackFlags( history, checkerboard, frame );
                const R::CloudTemporalFlags flags  = R::CloudTemporalDecodeFlags( packed );

                EXPECT_EQ( flags.HistoryValid, history );
                EXPECT_EQ( flags.Checkerboard, checkerboard );
                EXPECT_EQ( flags.Frame, frame & 1u );
            }
}

TEST( CloudTemporalPushConstant, TheThreeRowsAreTheRowsOfThePremultipliedPreviousMatrix )
{
    const glm::vec3  position( 1000.0f, 200000.0f, -3000.0f );
    const TestCamera previousCamera =
         MakeCamera( glm::vec3( 0.0f, 199000.0f, 0.0f ), glm::vec3( 0.1f, 0.0f, 1.0f ) );
    const TestCamera currentCamera = MakeCamera( position, glm::vec3( 0.0f, 0.0f, 1.0f ) );

    const glm::mat4         previous = previousCamera.ViewProjection();
    const CloudTemporalPush push     = PushFor( currentCamera, previousCamera, true );

    // A camera-relative offset through the rows must equal the absolute world point through the matrix.
    // This is the whole trick the 128-byte budget rests on, so it is checked against the matrix itself.
    const glm::vec3 relative( 500000.0f, 120000.0f, 2500000.0f );
    const glm::vec4 point( relative, 1.0f );
    const glm::vec4 absolute = previous * glm::vec4( position + relative, 1.0f );

    EXPECT_NEAR( glm::dot( push.PrevReprojectionRow0, point ) / absolute.x, 1.0f, 1e-5f );
    EXPECT_NEAR( glm::dot( push.PrevReprojectionRow1, point ) / absolute.y, 1.0f, 1e-5f );
    EXPECT_NEAR( glm::dot( push.PrevReprojectionRow3, point ) / absolute.w, 1.0f, 1e-5f );
}

TEST( CloudTemporalMemory, TheFourFiguresAreTheAllocatorsOwnArithmetic )
{
    using Desert::ECS::CloudResolutionScale;
    using Desert::ECS::CloudTemporalMode;

    constexpr uint32_t width  = 1920;
    constexpr uint32_t height = 1080;

    // 1920x1080 RGBA16F is 8 bytes a texel; the guide is RGBA8 at the same size, a quarter of that.
    const uint64_t fullColour = uint64_t( width ) * height * 8u;
    const uint64_t fullGuide  = uint64_t( width ) * height * 4u;
    const uint64_t halfColour = uint64_t( width / 2 ) * ( height / 2 ) * 8u;
    const uint64_t halfGuide  = uint64_t( width / 2 ) * ( height / 2 ) * 4u;

    EXPECT_EQ( CloudScaledImageBytes( width, height, CloudResolutionScale::Full, CloudTemporalMode::Reprojection ),
               3u * fullColour + fullGuide );
    EXPECT_EQ( CloudScaledImageBytes( width, height, CloudResolutionScale::Full, CloudTemporalMode::Off ),
               fullColour + fullGuide );
    EXPECT_EQ( CloudScaledImageBytes( width, height, CloudResolutionScale::Half, CloudTemporalMode::Reprojection ),
               3u * halfColour + halfGuide );
    EXPECT_EQ( CloudScaledImageBytes( width, height, CloudResolutionScale::Half, CloudTemporalMode::Off ),
               halfColour + halfGuide );

    // And in the units the requirement quotes, so a reviewer can compare without a calculator. CLD-34
    // said 47.5 MiB for the temporal configuration at Full; the depth guide the bilateral upsample needs
    // (CLD-32) was never budgeted in that table, and it is what takes the figure to 55.4.
    const double toMiB = 1.0 / ( 1024.0 * 1024.0 );
    EXPECT_NEAR( double( CloudScaledImageBytes( width, height, CloudResolutionScale::Full,
                                                CloudTemporalMode::Reprojection ) ) *
                      toMiB,
                 55.37, 0.05 );
    EXPECT_NEAR( double( CloudScaledImageBytes( width, height, CloudResolutionScale::Half,
                                                CloudTemporalMode::Reprojection ) ) *
                      toMiB,
                 13.84, 0.05 );
    EXPECT_NEAR(
         double( CloudScaledImageBytes( width, height, CloudResolutionScale::Full, CloudTemporalMode::Off ) ) *
              toMiB,
         23.73, 0.05 );
    EXPECT_NEAR(
         double( CloudScaledImageBytes( width, height, CloudResolutionScale::Half, CloudTemporalMode::Off ) ) *
              toMiB,
         5.93, 0.05 );
}

TEST( CloudTemporalMemory, TurningTheTemporalStageOffSavesExactlyTheHistoryPairAndNothingElse )
{
    using Desert::ECS::CloudResolutionScale;
    using Desert::ECS::CloudTemporalMode;
    using Desert::Graphic::CloudScaledExtent;

    // The guide and the scatter target are needed in BOTH modes — the bilateral upsample runs whether or
    // not there is a history (CLD-32a constraint 2) — so the whole difference between the two figures
    // must be the two history images, and nothing else.
    for ( CloudResolutionScale scale :
          { CloudResolutionScale::Quarter, CloudResolutionScale::Half, CloudResolutionScale::Full } )
    {
        const uint64_t colour = Desert::Core::Formats::CalculateImageSize(
             CloudScaledExtent( 1920, scale ), CloudScaledExtent( 1080, scale ),
             Desert::Core::Formats::ImageFormat::RGBA16F );

        const uint64_t on  = CloudScaledImageBytes( 1920, 1080, scale, CloudTemporalMode::Reprojection );
        const uint64_t off = CloudScaledImageBytes( 1920, 1080, scale, CloudTemporalMode::Off );

        EXPECT_EQ( on - off, 2u * colour );
    }
}

// ---- The two temporal fields reaching the GPU block --------------------------------------------------

namespace
{
    // The temporal knobs describe the ONE history this view has, so the packing takes them from the
    // primary (lowest) cloud layer and from nowhere else. A one-layer set is what every scene shipped
    // before two layers existed hands the renderer, and these tests go on asserting exactly what they
    // asserted about it.
    Desert::Graphic::CloudLayerSet PrimaryLayer( const Desert::ECS::VolumetricCloudData& data )
    {
        Desert::Graphic::CloudLayerSet set;
        set.Layers[0] = data;
        set.Count     = 1;
        return set;
    }
} // namespace

TEST( CloudTemporalPayload, TheTwoTemporalFieldsAreCarriedThroughToTheBlock )
{
    Desert::ECS::VolumetricCloudData data{};
    data.TemporalBlendFactor = 0.37f;
    data.TemporalClampScale  = 2.25f;

    const Desert::Graphic::CloudGpuPayload payload =
         PackCloudParams( PrimaryLayer( data ), Desert::Graphic::AtmosphereEnv{}, Desert::Graphic::WindEnv{}, 0.0f,
                          Desert::Graphic::CloudVoxelCounts{} );

    EXPECT_FLOAT_EQ( payload.TemporalBlendFactor, 0.37f );
    EXPECT_FLOAT_EQ( payload.TemporalClampScale, 2.25f );
}

TEST( CloudTemporalPayload, UnauthorableTemporalValuesAreRepairedAtTheBoundary )
{
    Desert::ECS::VolumetricCloudData data{};

    // A blend factor of zero never accepts a new frame: the sky would freeze on whatever it accumulated
    // first and stay there. The Details panel's range starts at 0.02 and so does the packing.
    data.TemporalBlendFactor = 0.0f;
    data.TemporalClampScale  = -1.0f;

    const Desert::Graphic::CloudGpuPayload payload =
         PackCloudParams( PrimaryLayer( data ), Desert::Graphic::AtmosphereEnv{}, Desert::Graphic::WindEnv{}, 0.0f,
                          Desert::Graphic::CloudVoxelCounts{} );

    EXPECT_FLOAT_EQ( payload.TemporalBlendFactor, 0.02f );
    EXPECT_GE( payload.TemporalClampScale, 0.0f );

    data.TemporalBlendFactor = 4.0f;
    const Desert::Graphic::CloudGpuPayload clamped =
         PackCloudParams( PrimaryLayer( data ), Desert::Graphic::AtmosphereEnv{}, Desert::Graphic::WindEnv{}, 0.0f,
                          Desert::Graphic::CloudVoxelCounts{} );
    EXPECT_FLOAT_EQ( clamped.TemporalBlendFactor, 1.0f );
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
