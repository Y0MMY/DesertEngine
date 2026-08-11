// The volumetric cloud maths, tested without a GPU.
//
// Two things are under test and they are different in kind:
//
//   1. THE SHADER'S OWN MATHS — CloudGeometryReference.hpp compiles the very GLSL header the raymarch
//      includes, so every assertion below is about the code that runs on the GPU rather than about a CPU
//      paraphrase of it. Shell intersection, step scheduling, the empty-space state machine, Beer,
//      powder, the phase functions, the height-dependent in-scatter term, the multi-scatter octaves, the
//      cone offsets and the depth reconstruction all live there.
//
//   2. THE CPU-SIDE PACKING — Graphic::PackCloudParams and the layout of the GPU block it fills. A field
//      that reaches the wrong offset is a frame in which everything after it is read from somebody
//      else's number, with no error message anywhere.
//
// Several tests exist because of a SPECIFIC defect confirmed in the reference implementation
// (Docs/Clouds/RESEARCH_REFERENCE.md J.3). Those are named on the test.

#include "CloudGeometryReference.hpp"

#include <Engine/Graphic/Clouds/CloudPayload.hpp>

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <vector>

namespace R = Desert::Tests::CloudGeometryRef;

using Desert::Graphic::CloudGpuPayload;
using Desert::Graphic::CloudRaymarchPush;
using Desert::Graphic::CloudResolutionDivisor;
using Desert::Graphic::CloudScaledExtent;
using Desert::Graphic::kCloudPayloadBytes;
using Desert::Graphic::PackCloudParams;

namespace
{
    // Earth-scale defaults, in the units each side of the boundary uses.
    constexpr float kPlanetRadiusWorld = 636000000.0f;                   // 6360 km in world units (cm)
    constexpr float kPlanetRadiusKm    = kPlanetRadiusWorld / 100000.0f; // 6360
    constexpr float kBottomKm          = 1.5f;
    constexpr float kThicknessKm       = 3.5f;

    // A double-precision reference for the same intersection, written the textbook way. Nothing about it
    // is shared with the header under test — that is the point: it is an independent answer.
    struct DoubleShell
    {
        bool   Hit;
        double TEnter;
        double TExit;
    };

    bool IntersectSphereDouble( const glm::dvec3& origin, const glm::dvec3& dir, const glm::dvec3& centre,
                                double radius, double& t0, double& t1 )
    {
        const glm::dvec3 oc = centre - origin;
        const double     b  = glm::dot( dir, oc );
        const double     c  = glm::dot( oc, oc ) - radius * radius;
        const double     d  = b * b - c;
        if ( d < 0.0 )
            return false;
        const double s = std::sqrt( d );
        t0             = b - s;
        t1             = b + s;
        return true;
    }

    // The same case selection as CloudShellBounds, in double, from absolute coordinates.
    DoubleShell ShellBoundsDouble( const glm::dvec3& cameraKm, const glm::dvec3& dir, double planetRadiusKm,
                                   double bottomKm, double thicknessKm )
    {
        const glm::dvec3 centre( 0.0, -planetRadiusKm, 0.0 );

        double ot0 = 0.0, ot1 = 0.0;
        if ( !IntersectSphereDouble( cameraKm, dir, centre, planetRadiusKm + bottomKm + thicknessKm, ot0, ot1 ) ||
             ot1 <= 0.0 )
            return DoubleShell{ false, 0.0, 0.0 };

        double     it0 = 0.0, it1 = 0.0;
        const bool innerHit = IntersectSphereDouble( cameraKm, dir, centre, planetRadiusKm + bottomKm, it0, it1 );

        double tEnter = std::max( ot0, 0.0 );
        double tExit  = ot1;
        if ( innerHit && it1 > 0.0 )
        {
            if ( it0 > 0.0 )
                tExit = it0;
            else
                tEnter = it1;
        }
        if ( tExit <= tEnter )
            return DoubleShell{ false, 0.0, 0.0 };
        return DoubleShell{ true, tEnter, tExit };
    }

    // The textbook ray/sphere quadratic, written the way the requirement's "world units vs kilometres"
    // comparison assumes it is written: dot(c,c) - r*r, with no algebraic expansion. Returns the FAR root
    // of the layer's inner sphere, which is where a camera below the layer enters it.
    float NaiveEntryRoot( float cameraAltitude, const glm::vec3& dir, float planetRadius, float bottom )
    {
        const glm::vec3 origin( 0.0f, cameraAltitude, 0.0f );
        const glm::vec3 centre( 0.0f, -planetRadius, 0.0f );
        const glm::vec3 oc     = centre - origin;
        const float     b      = glm::dot( dir, oc );
        const float     radius = planetRadius + bottom;
        const float     c      = glm::dot( oc, oc ) - radius * radius;
        const float     d      = b * b - c;
        if ( d < 0.0f )
            return -1.0f;
        return b + std::sqrt( d );
    }

    // Direction at `elevation` radians above the horizon, in the +X azimuth.
    glm::vec3 DirectionAboveHorizon( float elevation )
    {
        return glm::normalize( glm::vec3( std::cos( elevation ), std::sin( elevation ), 0.0f ) );
    }

    // The step schedule of CLD-25, as the shader walks it: no coarse tier, one step after another.
    struct ScheduledStep
    {
        float Distance;
        float Length;
    };

    std::vector<ScheduledStep> ScheduleSteps( float tEnter, float tExit, float minStep, float maxStep,
                                              float growth, int maxSteps )
    {
        std::vector<ScheduledStep> steps;
        float                      t = tEnter;
        for ( int i = 0; i < maxSteps && t < tExit; ++i )
        {
            const float length = R::CloudStepLength( t, minStep, maxStep, growth );
            steps.push_back( ScheduledStep{ t, length } );
            t += length;
        }
        return steps;
    }

    // Drives the empty-space state machine over a synthetic density field, exactly as the raymarch loop
    // does. Returns every sample that was taken, and whether it was a FINE one.
    struct MarchSample
    {
        float Distance;
        bool  Fine;
    };

    std::vector<MarchSample> RunMarch( float tEnter, float tExit, float slabStart, float slabEnd, float minStep,
                                       float maxStep, float growth, float coarseMultiplier, int emptyBeforeCoarse,
                                       int maxSteps )
    {
        std::vector<MarchSample> samples;
        R::CloudMarchState       state = R::CloudMarchBegin( tEnter );

        for ( int i = 0; i < maxSteps && state.T < tExit; ++i )
        {
            const bool occupied = state.T >= slabStart && state.T <= slabEnd;
            samples.push_back( MarchSample{ state.T, state.Fine } );
            state = R::CloudMarchAdvance( state, occupied, tEnter, minStep, maxStep, growth, coarseMultiplier,
                                          emptyBeforeCoarse );
        }
        return samples;
    }
} // namespace

// ---- CLD-23 / CLD-24: the shell -------------------------------------------------------------------

TEST( CloudShell, CameraInsideTheLayerLookingUpStartsAtZeroAndExitsThroughTheTop )
{
    const glm::vec3 camera( 0.0f, kBottomKm + 1.0f, 0.0f ); // 1 km into a 3.5 km layer
    const auto      hit =
         R::CloudShellBounds( camera, glm::vec3( 0.0f, 1.0f, 0.0f ), kPlanetRadiusKm, kBottomKm, kThicknessKm );

    ASSERT_TRUE( hit.Hit );
    EXPECT_FLOAT_EQ( hit.TEnter, 0.0f );
    EXPECT_NEAR( hit.TExit, kThicknessKm - 1.0f, 1e-2f ); // 2.5 km left to the top
}

TEST( CloudShell, CameraInsideTheLayerLookingDownEndsAtTheLayerFloor )
{
    const glm::vec3 camera( 0.0f, kBottomKm + 1.0f, 0.0f );
    const auto      hit =
         R::CloudShellBounds( camera, glm::vec3( 0.0f, -1.0f, 0.0f ), kPlanetRadiusKm, kBottomKm, kThicknessKm );

    ASSERT_TRUE( hit.Hit );
    EXPECT_FLOAT_EQ( hit.TEnter, 0.0f );
    EXPECT_NEAR( hit.TExit, 1.0f, 1e-2f );
}

TEST( CloudShell, CameraBelowTheLayerEntersAtTheBaseAndExitsAtTheTop )
{
    const glm::vec3 camera( 0.0f, 0.002f, 0.0f ); // two metres above the ground
    const auto      hit =
         R::CloudShellBounds( camera, glm::vec3( 0.0f, 1.0f, 0.0f ), kPlanetRadiusKm, kBottomKm, kThicknessKm );

    ASSERT_TRUE( hit.Hit );
    EXPECT_NEAR( hit.TEnter, kBottomKm - 0.002f, 1e-2f );
    EXPECT_NEAR( hit.TExit, kBottomKm + kThicknessKm - 0.002f, 1e-2f );
}

TEST( CloudShell, CameraAboveTheLayerLookingDownEntersAtTheTopAndStopsAtTheBase )
{
    const glm::vec3 camera( 0.0f, kBottomKm + kThicknessKm + 2.0f, 0.0f );
    const auto      hit =
         R::CloudShellBounds( camera, glm::vec3( 0.0f, -1.0f, 0.0f ), kPlanetRadiusKm, kBottomKm, kThicknessKm );

    ASSERT_TRUE( hit.Hit );
    EXPECT_NEAR( hit.TEnter, 2.0f, 1e-2f );
    EXPECT_NEAR( hit.TExit, 2.0f + kThicknessKm, 1e-2f );
}

// RESEARCH_REFERENCE J.3 #6: the reference's slab test took abs() of every plane distance, so a volume
// BEHIND the camera reported a hit. This is the test that would have caught it.
TEST( CloudShell, RayPointingAwayFromThePlanetMissesEntirely )
{
    const glm::vec3 camera( 0.0f, kBottomKm + kThicknessKm + 2.0f, 0.0f ); // above the layer
    const auto      hit =
         R::CloudShellBounds( camera, glm::vec3( 0.0f, 1.0f, 0.0f ), kPlanetRadiusKm, kBottomKm, kThicknessKm );

    EXPECT_FALSE( hit.Hit );
}

TEST( CloudShell, HorizontalRayFromInsideTheLayerStaysInsideForHundredsOfKilometres )
{
    const glm::vec3 camera( 0.0f, kBottomKm + 1.0f, 0.0f );
    const auto      hit =
         R::CloudShellBounds( camera, glm::vec3( 1.0f, 0.0f, 0.0f ), kPlanetRadiusKm, kBottomKm, kThicknessKm );

    ASSERT_TRUE( hit.Hit );
    EXPECT_FLOAT_EQ( hit.TEnter, 0.0f );
    // Tangent chord through a 2.5 km-thick cap on a 6361 km sphere: sqrt(2*R*h) is about 180 km.
    EXPECT_GT( hit.TExit, 100.0f );
}

// ---- CLD-24: precision ----------------------------------------------------------------------------
//
// The acceptance case, verbatim: a camera at 2 km altitude looking 5 degrees above the horizon, at Earth
// scale, must land within 0.1 % of a double reference — and the SAME formulation evaluated in world
// units (centimetres) must fail that tolerance, which is what documents why the scaling exists.

TEST( CloudShellPrecision, KilometreSpaceMatchesADoubleReferenceAtEarthScale )
{
    const glm::vec3 cameraKm( 0.0f, 2.0f, 0.0f );
    const glm::vec3 dir = DirectionAboveHorizon( glm::radians( 5.0f ) );

    const auto hit = R::CloudShellBounds( cameraKm, dir, kPlanetRadiusKm, kBottomKm, kThicknessKm );
    const auto ref =
         ShellBoundsDouble( glm::dvec3( cameraKm ), glm::dvec3( dir ), kPlanetRadiusKm, kBottomKm, kThicknessKm );

    ASSERT_TRUE( hit.Hit );
    ASSERT_TRUE( ref.Hit );
    EXPECT_LT( std::abs( hit.TExit - ref.TExit ) / ref.TExit, 0.001 );
}

// The requirement asked for this as "the world-unit formulation is asserted to FAIL the 0.1 %
// tolerance". Measured, it does not: at 2 km altitude and Earth scale the naive world-unit test is
// about 0.04 % out, which is inside 0.1 %. What is true, and what this asserts, is the ORDERING — the
// expanded camera-relative form is orders of magnitude better than kilometre space alone, which is in
// turn better than world units. The numbers are in the failure message so the next person does not have
// to re-derive them.
TEST( CloudShellPrecision, TheExpandedFormBeatsKilometreSpaceWhichBeatsWorldUnits )
{
    // A camera two metres up looking one degree above the horizon: the case where the cancellation in
    // dot(c,c) - r*r is worst, because 2*R*altitude is then five orders below the terms it comes from.
    constexpr double kCameraAltKm = 0.002;
    const glm::vec3  dir          = DirectionAboveHorizon( glm::radians( 1.0f ) );

    const auto ref = ShellBoundsDouble( glm::dvec3( 0.0, kCameraAltKm, 0.0 ), glm::dvec3( dir ), kPlanetRadiusKm,
                                        kBottomKm, kThicknessKm );
    ASSERT_TRUE( ref.Hit );

    // (1) The header's form, camera-relative and algebraically expanded.
    const auto hit = R::CloudShellBounds( glm::vec3( 0.0f, static_cast<float>( kCameraAltKm ), 0.0f ), dir,
                                          kPlanetRadiusKm, kBottomKm, kThicknessKm );
    ASSERT_TRUE( hit.Hit );
    const double expandedError = std::abs( hit.TEnter - ref.TEnter ) / ref.TEnter;

    // (2) The textbook quadratic in KILOMETRES: right scale, but dot(c,c) - r*r still cancels.
    const double naiveKmError =
         std::abs( NaiveEntryRoot( static_cast<float>( kCameraAltKm ), dir, kPlanetRadiusKm, kBottomKm ) -
                   ref.TEnter ) /
         ref.TEnter;

    // (3) The same quadratic in WORLD UNITS, where r*r is 4.0e17 and a float has no digits left for it.
    const double naiveWorldError = std::abs( NaiveEntryRoot( static_cast<float>( kCameraAltKm * 100000.0 ), dir,
                                                             kPlanetRadiusWorld, kBottomKm * 100000.0f ) /
                                                  100000.0f -
                                             ref.TEnter ) /
                                   ref.TEnter;

    EXPECT_LT( expandedError, 0.001 ) << "expanded form relative error " << expandedError;
    EXPECT_LT( expandedError * 10.0, naiveKmError )
         << "expanded " << expandedError << " vs naive km " << naiveKmError;
    EXPECT_LT( naiveKmError, naiveWorldError )
         << "naive km " << naiveKmError << " vs naive world units " << naiveWorldError;
}

TEST( CloudShellPrecision, ACameraTwoMetresUpStillAgreesWithTheDoubleReference )
{
    // The case the kilometre scaling alone does NOT fix: at two metres of altitude the cancellation in
    // dot(c,c) - r*r destroys four of seven digits even in km space. The header's expanded form has no
    // cancellation at all, and this is what asserts it.
    const glm::vec3 cameraKm( 0.0f, 0.002f, 0.0f );
    const glm::vec3 dir = DirectionAboveHorizon( glm::radians( 2.0f ) );

    const auto hit = R::CloudShellBounds( cameraKm, dir, kPlanetRadiusKm, kBottomKm, kThicknessKm );
    const auto ref =
         ShellBoundsDouble( glm::dvec3( cameraKm ), glm::dvec3( dir ), kPlanetRadiusKm, kBottomKm, kThicknessKm );

    ASSERT_TRUE( hit.Hit );
    ASSERT_TRUE( ref.Hit );
    EXPECT_LT( std::abs( hit.TEnter - ref.TEnter ) / ref.TEnter, 0.001 );
}

TEST( CloudHeightFraction, RunsFromZeroAtTheBaseToOneAtTheTop )
{
    EXPECT_NEAR(
         R::CloudHeightFraction( glm::vec3( 0.0f, kBottomKm, 0.0f ), kPlanetRadiusKm, kBottomKm, kThicknessKm ),
         0.0f, 1e-3f );
    EXPECT_NEAR( R::CloudHeightFraction( glm::vec3( 0.0f, kBottomKm + kThicknessKm, 0.0f ), kPlanetRadiusKm,
                                         kBottomKm, kThicknessKm ),
                 1.0f, 1e-3f );
    EXPECT_NEAR( R::CloudHeightFraction( glm::vec3( 0.0f, kBottomKm + 0.5f * kThicknessKm, 0.0f ), kPlanetRadiusKm,
                                         kBottomKm, kThicknessKm ),
                 0.5f, 1e-3f );
}

TEST( CloudHeightFraction, TheUnclampedFormReportsLeavingTheLayerInBothDirections )
{
    // The light march needs this: a cone sample that has flown out of the layer must be recognisable as
    // outside it. Clamped, it would sit exactly on the surface it passed, and every cloud top would
    // shadow itself with a copy of itself.
    EXPECT_LT( R::CloudLayerHeight( glm::vec3( 0.0f, kBottomKm - 0.5f, 0.0f ), kPlanetRadiusKm, kBottomKm,
                                    kThicknessKm ),
               0.0f );
    EXPECT_GT( R::CloudLayerHeight( glm::vec3( 0.0f, kBottomKm + kThicknessKm + 0.5f, 0.0f ), kPlanetRadiusKm,
                                    kBottomKm, kThicknessKm ),
               1.0f );
    // Inside the layer the two forms agree exactly.
    const glm::vec3 inside( 0.0f, kBottomKm + 0.25f * kThicknessKm, 0.0f );
    EXPECT_FLOAT_EQ( R::CloudLayerHeight( inside, kPlanetRadiusKm, kBottomKm, kThicknessKm ),
                     R::CloudHeightFraction( inside, kPlanetRadiusKm, kBottomKm, kThicknessKm ) );
}

TEST( CloudHeightFraction, FollowsThePlanetCurvatureRatherThanWorldY )
{
    // 200 km along the ground, at the altitude of the layer base. On a flat model the height fraction
    // there would still be 0; on the shell the surface has dropped away by about 3 km, so the same world
    // Y is well up inside the layer. Getting this wrong flattens every distant cloud into the floor.
    // Not named `far`: <windows.h> still #defines `near` and `far` to nothing (16-bit segment
    // qualifiers), gtest reaches that header on Windows, and the declaration below then compiles to
    // `const glm::vec3 ( ... )`.
    const glm::vec3 farAlongGround( 200.0f, kBottomKm, 0.0f );
    EXPECT_GT( R::CloudHeightFraction( farAlongGround, kPlanetRadiusKm, kBottomKm, kThicknessKm ), 0.5f );
}

// ---- Units ----------------------------------------------------------------------------------------

TEST( CloudUnits, KilometresAndWorldUnitsRoundTrip )
{
    EXPECT_FLOAT_EQ( R::CloudWorldFromKm( 1.0f ), 100000.0f ); // 1 km = 100 000 cm
    EXPECT_FLOAT_EQ( R::CloudKmFromWorld( 100000.0f ), 1.0f );
    EXPECT_NEAR( R::CloudKmFromWorld( R::CloudWorldFromKm( 6360.0f ) ), 6360.0f, 1e-3f );
    // The requirement's own conversion: PlanetRadius is authored in world units at Earth scale.
    EXPECT_FLOAT_EQ( R::CloudKmFromWorld( kPlanetRadiusWorld ), 6360.0f );
}

// ---- CLD-25: the step schedule --------------------------------------------------------------------

TEST( CloudSteps, StepsGrowWithDistanceAndStayInsideTheAuthoredBounds )
{
    constexpr float kMin    = 1500.0f;  // 15 m
    constexpr float kMax    = 70000.0f; // 700 m
    constexpr float kGrowth = 0.008f;

    const auto steps = ScheduleSteps( 0.0f, 15000000.0f, kMin, kMax, kGrowth, 4096 );

    ASSERT_FALSE( steps.empty() );
    for ( size_t i = 0; i < steps.size(); ++i )
    {
        EXPECT_GE( steps[i].Length, kMin );
        EXPECT_LE( steps[i].Length, kMax );
        if ( i > 0 )
        {
            EXPECT_GT( steps[i].Distance, steps[i - 1].Distance );
            EXPECT_GE( steps[i].Length, steps[i - 1].Length );
        }
    }
    // Distance-adaptive means the far steps are genuinely longer than the near ones.
    EXPECT_GT( steps.back().Length, steps.front().Length * 2.0f );
}

TEST( CloudSteps, TheScheduleCoversTheWholeIntervalWhenMaxStepsIsNotBinding )
{
    const auto steps = ScheduleSteps( 1000.0f, 200000.0f, 1500.0f, 70000.0f, 0.008f, 4096 );

    ASSERT_FALSE( steps.empty() );
    EXPECT_GE( steps.front().Distance, 1000.0f );
    EXPECT_LT( steps.back().Distance, 200000.0f );
    EXPECT_GE( steps.back().Distance + steps.back().Length, 200000.0f );
}

// CLD-62: MaxSteps is a hard bound the loop honours, not a hint. The adversarial case is the finest
// step over the longest view distance.
TEST( CloudSteps, MaxStepsBoundsTheLoopForAdversarialSettings )
{
    constexpr int kMaxSteps = 32;
    const auto    steps     = ScheduleSteps( 0.0f, 40000000.0f /* 400 km */, 100.0f /* 1 m */, 5000.0f,
                                             0.0f /* no growth: the worst case */, kMaxSteps );
    EXPECT_EQ( static_cast<int>( steps.size() ), kMaxSteps );
}

// ---- CLD-26: the empty-space skip -----------------------------------------------------------------

TEST( CloudMarch, FineSamplingCoversTheWholeOccupiedIntervalAndCostsFarLessThanAUniformMarch )
{
    constexpr float kEnter   = 0.0f;
    constexpr float kExit    = 100000.0f;
    constexpr float kSlabLo  = 30000.0f; // the occupied 30-40 % of the ray
    constexpr float kSlabHi  = 40000.0f;
    constexpr float kMinStep = 500.0f;
    constexpr float kMaxStep = 5000.0f;
    constexpr float kGrowth  = 0.0f;
    constexpr float kCoarse  = 3.0f;
    constexpr int   kEmpty   = 8;

    const auto samples =
         RunMarch( kEnter, kExit, kSlabLo, kSlabHi, kMinStep, kMaxStep, kGrowth, kCoarse, kEmpty, 4096 );

    // (a) The whole occupied interval is covered by FINE samples, with no gap wider than one fine step.
    float lastFine = -1.0f;
    bool  sawFine  = false;
    for ( const auto& s : samples )
    {
        if ( !s.Fine || s.Distance < kSlabLo || s.Distance > kSlabHi )
            continue;
        if ( sawFine )
            EXPECT_LE( s.Distance - lastFine, kMinStep + 1e-3f );
        lastFine = s.Distance;
        sawFine  = true;
    }
    ASSERT_TRUE( sawFine );
    EXPECT_LE( lastFine + kMinStep, kSlabHi + kMinStep );

    // (b) Far cheaper than marching the whole ray at the fine step.
    const size_t uniform = static_cast<size_t>( ( kExit - kEnter ) / kMinStep );
    EXPECT_LT( samples.size() * 2, uniform );

    // (c) The step-back was exercised: a fine sample exists at or before the slab's leading edge, which
    //     only happens because the coarse tier rewinds one stride when it first sees density.
    bool coveredEntry = false;
    for ( const auto& s : samples )
        if ( s.Fine && s.Distance <= kSlabLo )
            coveredEntry = true;
    EXPECT_TRUE( coveredEntry );
}

TEST( CloudMarch, ReturnsToTheCoarseTierAfterTheAuthoredRunOfEmptyFineSamples )
{
    constexpr int kEmpty = 4;
    const auto    samples =
         RunMarch( 0.0f, 100000.0f, 10000.0f, 12000.0f, 500.0f, 5000.0f, 0.0f, 3.0f, kEmpty, 4096 );

    // After the slab there must be coarse samples again — otherwise the "skip" only ever skips once.
    bool coarseAfterSlab = false;
    for ( const auto& s : samples )
        if ( !s.Fine && s.Distance > 14000.0f )
            coarseAfterSlab = true;
    EXPECT_TRUE( coarseAfterSlab );
}

TEST( CloudMarch, StartsCoarseSoAnEmptySkyCostsAlmostNothing )
{
    const auto samples = RunMarch( 0.0f, 100000.0f, 1e9f, 1e9f, 500.0f, 5000.0f, 0.0f, 3.0f, 8, 4096 );
    for ( const auto& s : samples )
        EXPECT_FALSE( s.Fine );
    EXPECT_LT( samples.size(), static_cast<size_t>( 100000.0f / ( 500.0f * 3.0f ) ) + 2u );
}

// ---- CLD-25: Beer -----------------------------------------------------------------------------------

TEST( CloudBeer, IsOneAtZeroMonotoneAndMultiplicative )
{
    EXPECT_FLOAT_EQ( R::CloudBeerTransmittance( 0.0f ), 1.0f );
    EXPECT_NEAR( R::CloudBeerTransmittance( 200.0f ), 0.0f, 1e-6f );

    float previous = 1.0f;
    for ( float tau = 0.0f; tau < 20.0f; tau += 0.25f )
    {
        const float t = R::CloudBeerTransmittance( tau );
        EXPECT_LE( t, previous + 1e-7f );
        previous = t;
    }

    const float a = 0.7f;
    const float b = 1.9f;
    EXPECT_NEAR( R::CloudBeerTransmittance( a + b ),
                 R::CloudBeerTransmittance( a ) * R::CloudBeerTransmittance( b ), 1e-5f );
}

TEST( CloudPowder, DarkensThinEdgesAndVanishesWhereItIsTurnedOff )
{
    EXPECT_FLOAT_EQ( R::CloudPowder( 0.5f, 0.0f, 2.0f ), 1.0f ); // strength 0 = no effect at all
    EXPECT_LT( R::CloudPowder( 0.0f, 1.0f, 2.0f ), R::CloudPowder( 1.0f, 1.0f, 2.0f ) );
    EXPECT_NEAR( R::CloudPowder( 0.0f, 1.0f, 2.0f ), 0.0f, 1e-6f );
}

// ---- CLD-27: the phase functions --------------------------------------------------------------------

TEST( CloudPhase, IsotropicAtZeroEccentricity )
{
    const float isotropic = 1.0f / ( 4.0f * 3.141592653589793f );
    for ( float c = -1.0f; c <= 1.0f; c += 0.1f )
        EXPECT_NEAR( R::CloudHenyeyGreenstein( c, 0.0f ), isotropic, 1e-6f );
}

// RESEARCH_REFERENCE J.3 #3: the reference passed g = 1.0, which makes (1 - g^2) exactly zero and
// deletes the forward lobe. This is the test that would have caught it.
TEST( CloudPhase, ForwardLobeIsStrictlyIncreasingInCosineAndPeaksAtOne )
{
    constexpr float kG       = 0.8f;
    float           previous = R::CloudHenyeyGreenstein( -1.0f, kG );
    for ( float c = -0.9f; c <= 1.0f; c += 0.05f )
    {
        const float value = R::CloudHenyeyGreenstein( c, kG );
        EXPECT_GT( value, previous );
        previous = value;
    }
    EXPECT_GT( R::CloudHenyeyGreenstein( 1.0f, kG ), R::CloudHenyeyGreenstein( 0.99f, kG ) );
    EXPECT_GT( R::CloudHenyeyGreenstein( 1.0f, kG ), 0.0f );
}

TEST( CloudPhase, DualLobeIsPositiveEverywhereOverTheAuthorableRange )
{
    for ( float fwd = 0.0f; fwd <= 0.99f; fwd += 0.33f )
        for ( float back = -0.99f; back <= 0.0f; back += 0.33f )
            for ( float blend = 0.0f; blend <= 1.0f; blend += 0.25f )
                for ( float c = -1.0f; c <= 1.0f; c += 0.25f )
                {
                    const float p = R::CloudDualLobePhase( c, fwd, back, blend, 1.2f );
                    EXPECT_GT( p, 0.0f );
                    EXPECT_TRUE( std::isfinite( p ) );
                }
}

TEST( CloudPhase, SilverLiningRaisesTheForwardLobeOnly )
{
    const float towardSun = R::CloudDualLobePhase( 1.0f, 0.8f, -0.15f, 1.0f, 2.0f );
    const float plain     = R::CloudDualLobePhase( 1.0f, 0.8f, -0.15f, 1.0f, 1.0f );
    EXPECT_NEAR( towardSun, plain * 2.0f, 1e-4f );

    // Blend 0 selects the backward lobe, which the silver-lining multiplier must not touch.
    EXPECT_FLOAT_EQ( R::CloudDualLobePhase( -1.0f, 0.8f, -0.15f, 0.0f, 4.0f ),
                     R::CloudDualLobePhase( -1.0f, 0.8f, -0.15f, 0.0f, 1.0f ) );
}

// RESEARCH_REFERENCE J.3 #4: the reference substituted the literal 0.5 for the height fraction in both
// in-scatter terms, so its vertical gradient was a constant. This is the test that would have caught it.
TEST( CloudInScatter, VariesWithTheHeightFractionRatherThanBeingConstant )
{
    const float atBase = R::CloudInScatterProbability( 0.02f, 0.5f, 0.4f );
    const float atMid  = R::CloudInScatterProbability( 0.50f, 0.5f, 0.4f );
    const float atTop  = R::CloudInScatterProbability( 0.98f, 0.5f, 0.4f );

    EXPECT_LT( atBase, atMid );
    EXPECT_NE( atMid, atTop );
    EXPECT_GT( atTop - atBase, 0.05f );
}

TEST( CloudInScatter, StaysInsideZeroToOne )
{
    for ( float h = 0.0f; h <= 1.0f; h += 0.05f )
        for ( float d = 0.0f; d <= 1.0f; d += 0.25f )
            for ( float tau = 0.0f; tau <= 4.0f; tau += 1.0f )
            {
                const float p = R::CloudInScatterProbability( h, d, tau );
                EXPECT_GE( p, 0.0f );
                EXPECT_LE( p, 1.0f );
            }
}

// ---- CLD-28: multiple scattering ------------------------------------------------------------------

TEST( CloudMultiScatter, OneOctaveReproducesSingleScatteringExactly )
{
    constexpr float kTau = 1.3f;
    constexpr float kCos = 0.4f;
    const glm::vec3 tint( 1.0f );

    const glm::vec3 ms = R::CloudMultiScatter( kTau, tint, kCos, 1, 1.0f, 1.0f, 1.0f, 0.8f, -0.15f, 0.5f, 1.2f );
    const float     single =
         R::CloudBeerTransmittance( kTau ) * R::CloudDualLobePhase( kCos, 0.8f, -0.15f, 0.5f, 1.2f );

    EXPECT_FLOAT_EQ( ms.x, single );
    EXPECT_FLOAT_EQ( ms.y, single );
    EXPECT_FLOAT_EQ( ms.z, single );
}

TEST( CloudMultiScatter, AddingOctavesIsMonotonicallyBrightening )
{
    constexpr float kTau = 2.0f;
    const glm::vec3 tint( 1.0f );

    float previous = 0.0f;
    for ( int octaves = 1; octaves <= 4; ++octaves )
    {
        const glm::vec3 ms =
             R::CloudMultiScatter( kTau, tint, 0.2f, octaves, 0.5f, 0.5f, 0.5f, 0.8f, -0.15f, 0.5f, 1.2f );
        EXPECT_GT( ms.x, previous );
        previous = ms.x;
    }
}

TEST( CloudMultiScatter, TheExtinctionTintActsPerChannel )
{
    const glm::vec3 tint( 1.0f, 0.5f, 0.25f );
    const glm::vec3 ms = R::CloudMultiScatter( 2.0f, tint, 0.2f, 2, 0.5f, 0.5f, 0.5f, 0.8f, -0.15f, 0.5f, 1.2f );
    EXPECT_LT( ms.x, ms.y );
    EXPECT_LT( ms.y, ms.z ); // less extinction survives more light
}

// ---- CLD-27: the cone march -----------------------------------------------------------------------
//
// RESEARCH_REFERENCE J.3 #2: the reference's light grid marched AWAY from the sun, so its self-shadowing
// came from the wrong side. Every sample here is asserted to lie toward the sun.

TEST( CloudConeMarch, EverySampleLiesTowardTheSun )
{
    const glm::vec3 suns[] = { glm::normalize( glm::vec3( 0.3f, 0.9f, 0.25f ) ),
                               glm::vec3( 0.0f, 1.0f, 0.0f ),  // straight up: the degenerate basis case
                               glm::vec3( 0.0f, -1.0f, 0.0f ), // straight down
                               glm::normalize( glm::vec3( 1.0f, 0.05f, 0.0f ) ) };

    for ( const glm::vec3& sun : suns )
        for ( int count = 1; count <= 16; ++count )
            for ( int i = 0; i < count; ++i )
            {
                const glm::vec3 offset = R::CloudConeSampleOffset( sun, i, count, 100000.0f, 1.0f );
                EXPECT_GT( glm::dot( offset, sun ), 0.0f );
                EXPECT_TRUE( std::isfinite( offset.x ) );
                EXPECT_TRUE( std::isfinite( offset.y ) );
                EXPECT_TRUE( std::isfinite( offset.z ) );
            }
}

TEST( CloudConeMarch, TheSampleWeightsPartitionTheMarchDistance )
{
    constexpr float kDistance = 100000.0f;
    for ( int count = 1; count <= 16; ++count )
    {
        float total = 0.0f;
        for ( int i = 0; i < count; ++i )
        {
            const float w = R::CloudConeSampleWeight( i, count, kDistance );
            EXPECT_GT( w, 0.0f );
            total += w;
        }
        EXPECT_NEAR( total, kDistance, kDistance * 1e-4f );
    }
}

TEST( CloudConeMarch, SamplesAreDenserNearTheShadedPointThanFarFromIt )
{
    EXPECT_LT( R::CloudConeSampleWeight( 0, 8, 100000.0f ), R::CloudConeSampleWeight( 7, 8, 100000.0f ) );
}

TEST( CloudConeMarch, ZeroSpreadGivesAStraightRayAlongTheSun )
{
    const glm::vec3 sun = glm::normalize( glm::vec3( 0.3f, 0.9f, 0.25f ) );
    for ( int i = 0; i < 8; ++i )
    {
        const glm::vec3 offset = R::CloudConeSampleOffset( sun, i, 8, 100000.0f, 0.0f );
        EXPECT_NEAR( glm::length( glm::cross( offset, sun ) ), 0.0f, 1e-1f );
    }
}

// ---- CLD-72: the ambient composition ----------------------------------------------------------------

TEST( CloudAmbient, MultipliersAtOneAndAFullHeightBiasSelectTheTwoEndpointsExactly )
{
    const glm::vec3 sky( 0.2f, 0.4f, 0.9f );
    const glm::vec3 ground( 0.1f, 0.09f, 0.08f );

    EXPECT_EQ( R::CloudAmbient( sky, ground, 1.0f, 1.0f, 1.0f, 1.0f ), sky );
    EXPECT_EQ( R::CloudAmbient( sky, ground, 1.0f, 1.0f, 1.0f, 0.0f ), ground );
}

TEST( CloudAmbient, ZeroHeightBiasRemovesTheHeightDependenceEntirely )
{
    const glm::vec3 sky( 0.2f, 0.4f, 0.9f );
    const glm::vec3 ground( 0.1f, 0.09f, 0.08f );

    const glm::vec3 low  = R::CloudAmbient( sky, ground, 1.0f, 1.0f, 0.0f, 0.0f );
    const glm::vec3 high = R::CloudAmbient( sky, ground, 1.0f, 1.0f, 0.0f, 1.0f );
    EXPECT_EQ( low, high );
    EXPECT_EQ( low, ( sky + ground ) * 0.5f );
}

TEST( CloudAmbient, TheSkyMultiplierZeroesOnlyTheSkyTerm )
{
    const glm::vec3 sky( 0.2f, 0.4f, 0.9f );
    const glm::vec3 ground( 0.1f, 0.09f, 0.08f );

    EXPECT_EQ( R::CloudAmbient( sky, ground, 0.0f, 1.0f, 1.0f, 1.0f ), glm::vec3( 0.0f ) );
    EXPECT_EQ( R::CloudAmbient( sky, ground, 0.0f, 1.0f, 1.0f, 0.0f ), ground );
}

// ---- CLD-29: the depth reconstruction ---------------------------------------------------------------

TEST( CloudDepth, ReconstructionIsTheInverseOfTheEnginesOwnProjection )
{
    // The engine's camera builds its projection with glm::perspective and its view with glm::lookAt; the
    // reconstruction has to be the inverse of THAT, whatever depth convention glm was configured with.
    const glm::mat4 projection = glm::perspective( glm::radians( 60.0f ), 16.0f / 9.0f, 10.0f, 500000.0f );
    const glm::vec3 cameraPos( 120.0f, 300.0f, -45.0f );
    const glm::mat4 view =
         glm::lookAt( cameraPos, cameraPos + glm::vec3( 0.3f, -0.2f, 1.0f ), glm::vec3( 0.0f, 1.0f, 0.0f ) );

    const glm::mat4 viewProjection = projection * view;
    const glm::mat4 inverse        = glm::inverse( viewProjection );

    const glm::vec3 points[] = { cameraPos + glm::vec3( 30.0f, -20.0f, 100.0f ),
                                 cameraPos + glm::vec3( -500.0f, 200.0f, 4000.0f ),
                                 cameraPos + glm::vec3( 900.0f, -100.0f, 40000.0f ) };

    for ( const glm::vec3& p : points )
    {
        const glm::vec4 clip = viewProjection * glm::vec4( p, 1.0f );
        ASSERT_GT( clip.w, 0.0f );
        const glm::vec3 ndc( clip.x / clip.w, clip.y / clip.w, clip.z / clip.w );

        const float reconstructed =
             R::CloudDistanceFromDepth( inverse, cameraPos, glm::vec2( ndc.x, ndc.y ), ndc.z );
        const float expected = glm::length( p - cameraPos );

        // 1e-3 and not tighter: the reconstruction runs a 40 km point back through a single-precision
        // inverse of a projection whose far plane is 5 km beyond it, and 2e-4 of relative error there is
        // eight metres. Cloud occlusion is decided at kilometre scale, so this is three orders of
        // magnitude finer than it needs to be — but it is a REAL bound, not a rounded-up one.
        EXPECT_NEAR( reconstructed / expected, 1.0f, 1e-3f );
    }
}

TEST( CloudDepth, ClearedDepthMeansSkyAllTheWayToTheViewDistance )
{
    const glm::mat4 identity( 1.0f );
    EXPECT_FLOAT_EQ( R::CloudGeometryLimit( identity, glm::vec3( 0.0f ), glm::vec2( 0.0f ), 1.0f, 12345.0f ),
                     12345.0f );
}

TEST( CloudDepth, GeometryNeverExtendsTheMarchBeyondTheViewDistance )
{
    const glm::mat4 projection = glm::perspective( glm::radians( 60.0f ), 1.0f, 10.0f, 500000.0f );
    const glm::mat4 view =
         glm::lookAt( glm::vec3( 0.0f ), glm::vec3( 0.0f, 0.0f, 1.0f ), glm::vec3( 0.0f, 1.0f, 0.0f ) );
    const glm::mat4 inverse = glm::inverse( projection * view );

    const glm::vec4 clip = projection * view * glm::vec4( 0.0f, 0.0f, 400000.0f, 1.0f );
    const float     ndcZ = clip.z / clip.w;

    EXPECT_FLOAT_EQ( R::CloudGeometryLimit( inverse, glm::vec3( 0.0f ), glm::vec2( 0.0f ), ndcZ, 1000.0f ),
                     1000.0f );
}

// ---- The GPU payload --------------------------------------------------------------------------------

TEST( CloudPayload, TheBlockIsTheSizeTheBufferIsCreatedWith )
{
    // std430 rounds the block up to its 16-byte alignment; the buffer must cover that, not sizeof.
    // 492 and not 484 since the temporal stage landed: Temporal Blend Factor and Temporal Clamp Scale are
    // read by the resolve shader and so are two more floats in the block. The rounded size is unchanged.
    EXPECT_EQ( sizeof( CloudGpuPayload ), 492u );
    EXPECT_EQ( kCloudPayloadBytes, 496u );
    EXPECT_GE( kCloudPayloadBytes, sizeof( CloudGpuPayload ) );
    EXPECT_EQ( kCloudPayloadBytes % 16u, 0u );
}

TEST( CloudPayload, ThePushConstantFitsTheGuaranteedRange )
{
    EXPECT_LE( sizeof( CloudRaymarchPush ), 128u );
    EXPECT_EQ( sizeof( CloudRaymarchPush ), 80u );
}

TEST( CloudPayload, EveryFieldReachesADistinctOffset )
{
    // A spot check of the group boundaries: an insertion anywhere shifts one of these. The full set is
    // asserted at compile time by the static_asserts in CloudPayload.hpp.
    EXPECT_EQ( offsetof( CloudGpuPayload, SunDirection ), 0u );
    EXPECT_EQ( offsetof( CloudGpuPayload, SceneWind ), 176u );
    EXPECT_EQ( offsetof( CloudGpuPayload, PlanetRadius ), 192u );
    EXPECT_EQ( offsetof( CloudGpuPayload, Coverage ), 216u );
    EXPECT_EQ( offsetof( CloudGpuPayload, LightMarchDistance ), 356u );
    EXPECT_EQ( offsetof( CloudGpuPayload, MinStepSize ), 440u );
    EXPECT_EQ( offsetof( CloudGpuPayload, TemporalBlendFactor ), 460u );
    EXPECT_EQ( offsetof( CloudGpuPayload, MultiScatterOctaves ), 488u );
}

TEST( CloudPayload, PackingCarriesTheComponentAndTheAtmosphereThrough )
{
    Desert::ECS::VolumetricCloudData data{};
    Desert::Graphic::AtmosphereEnv   atmosphere{};
    Desert::Graphic::WindEnv         wind{};

    atmosphere.SunDirection     = glm::vec3( 0.0f, 1.0f, 0.0f );
    atmosphere.SunIrradiance    = glm::vec3( 3.0f, 2.5f, 2.0f );
    atmosphere.ZenithRadiance   = glm::vec3( 0.1f, 0.2f, 0.5f );
    atmosphere.GroundRadiance   = glm::vec3( 0.05f, 0.04f, 0.03f );
    atmosphere.SunAngularRadius = 0.02f;
    atmosphere.PlanetRadius     = kPlanetRadiusWorld;

    wind.Direction = glm::vec2( 1.0f, 0.0f );
    wind.Strength  = 0.15f;

    const CloudGpuPayload p = PackCloudParams( data, atmosphere, wind, 12.5f );

    // The planet radius comes from the atmosphere and from nowhere else — the cloud subsystem is
    // forbidden a radius of its own.
    EXPECT_FLOAT_EQ( p.PlanetRadius, kPlanetRadiusWorld );
    EXPECT_FLOAT_EQ( p.SunDirection.w, atmosphere.SunAngularRadius );
    EXPECT_EQ( glm::vec3( p.SunIrradiance ), atmosphere.SunIrradiance );
    EXPECT_EQ( glm::vec3( p.ZenithRadiance ), atmosphere.ZenithRadiance );
    EXPECT_EQ( glm::vec3( p.GroundRadiance ), atmosphere.GroundRadiance );

    // The scene wind becomes a velocity in world units per second; the component contributes a
    // multiplier and an angle, never a second direction.
    EXPECT_FLOAT_EQ( p.SceneWind.x, 0.15f * Desert::Graphic::kCloudWindSpeedPerStrength );
    EXPECT_FLOAT_EQ( p.SceneWind.y, 0.0f );
    EXPECT_FLOAT_EQ( p.SceneWind.w, 12.5f );

    // Defaults are the Partly Cloudy preset and must survive the trip unchanged.
    EXPECT_FLOAT_EQ( p.Coverage, data.Coverage );
    EXPECT_FLOAT_EQ( p.ExtinctionTint.w, data.ExtinctionScale );
    EXPECT_EQ( p.MaxSteps, data.MaxSteps );
    EXPECT_EQ( p.LightMarchSamples, data.LightMarchSamples );
    EXPECT_EQ( p.WeatherOctaves, data.WeatherOctaves );
}

// CLD-63: the ordering invariants are what produce a division by zero or a negative range in the
// shader, and they are authored independently in the Details panel.
TEST( CloudPayload, InvertedRangesAreRepairedAtTheBoundary )
{
    Desert::ECS::VolumetricCloudData data{};
    Desert::Graphic::AtmosphereEnv   atmosphere{};
    Desert::Graphic::WindEnv         wind{};

    data.HorizonFadeStart       = 900.0f;
    data.HorizonFadeEnd         = 100.0f;
    data.NearFadeStart          = 50.0f;
    data.NearFadeEnd            = 10.0f;
    data.SofteningStartDistance = 4000.0f;
    data.SofteningEndDistance   = 100.0f;
    data.DistanceFadeStart      = 8000.0f;
    data.DistanceFadeEnd        = 20.0f;
    data.HighFreqFadeStart      = 700.0f;
    data.HighFreqFadeEnd        = 20.0f;
    data.MinStepSize            = 5000.0f;
    data.MaxStepSize            = 100.0f;

    const CloudGpuPayload p = PackCloudParams( data, atmosphere, wind, 0.0f );

    EXPECT_GE( p.HorizonFadeEnd, p.HorizonFadeStart );
    EXPECT_GE( p.NearFadeEnd, p.NearFadeStart );
    EXPECT_GE( p.SofteningEndDistance, p.SofteningStartDistance );
    EXPECT_GE( p.DistanceFadeEnd, p.DistanceFadeStart );
    EXPECT_GE( p.HighFreqFadeEnd, p.HighFreqFadeStart );
    EXPECT_GE( p.MaxStepSize, p.MinStepSize );
}

TEST( CloudPayload, EveryPackedValueIsFinite )
{
    Desert::ECS::VolumetricCloudData data{};
    Desert::Graphic::AtmosphereEnv   atmosphere{};
    Desert::Graphic::WindEnv         wind{};
    atmosphere.PlanetRadius = kPlanetRadiusWorld;

    const CloudGpuPayload p     = PackCloudParams( data, atmosphere, wind, 3.0f );
    const float*          words = reinterpret_cast<const float*>( &p );

    // The trailing six members are ints; everything before them is a float and must be finite.
    const size_t floatCount = offsetof( CloudGpuPayload, WeatherSeed ) / sizeof( float );
    for ( size_t i = 0; i < floatCount; ++i )
        EXPECT_TRUE( std::isfinite( words[i] ) ) << "word " << i;
}

TEST( CloudResolution, EachTierScalesTheTargetAndNeverProducesAZeroExtent )
{
    using Desert::ECS::CloudResolutionScale;

    EXPECT_EQ( CloudResolutionDivisor( CloudResolutionScale::Full ), 1u );
    EXPECT_EQ( CloudResolutionDivisor( CloudResolutionScale::Half ), 2u );
    EXPECT_EQ( CloudResolutionDivisor( CloudResolutionScale::Quarter ), 4u );

    EXPECT_EQ( CloudScaledExtent( 1920u, CloudResolutionScale::Full ), 1920u );
    EXPECT_EQ( CloudScaledExtent( 1920u, CloudResolutionScale::Half ), 960u );
    EXPECT_EQ( CloudScaledExtent( 1920u, CloudResolutionScale::Quarter ), 480u );

    // A viewport can be dragged down to a couple of pixels; a zero-sized image fails to create and
    // takes the whole pass down with it.
    EXPECT_EQ( CloudScaledExtent( 1u, CloudResolutionScale::Quarter ), 1u );
    EXPECT_EQ( CloudScaledExtent( 0u, CloudResolutionScale::Half ), 1u );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
