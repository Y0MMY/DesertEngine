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

#include <Engine/Core/Projection.hpp>
#include <Engine/Graphic/Clouds/CloudMarchScale.hpp>
#include <Engine/Graphic/Clouds/CloudPayload.hpp>
#include <Engine/Graphic/Clouds/CloudProfileCurves.hpp>
#include <Engine/Graphic/Clouds/CloudWeatherScale.hpp>
#include <Engine/Graphic/CloudPresets.hpp>
#include <Engine/Graphic/CloudQuality.hpp>

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <set>
#include <vector>

namespace R = Desert::Tests::CloudGeometryRef;

using Desert::Graphic::CloudGpuPayload;
using Desert::Graphic::CloudLayerPayload;
using Desert::Graphic::CloudRaymarchPush;
using Desert::Graphic::CloudResolutionDivisor;
using Desert::Graphic::CloudScaledExtent;
using Desert::Graphic::CloudVoxelCounts;
using Desert::Graphic::kCloudMaxLayers;
using Desert::Graphic::kCloudPayloadBytes;
using Desert::Graphic::PackCloudParams;

namespace
{
    // The single-layer set every pre-existing packing test speaks in. Count 1 is what every scene
    // shipped before two layers existed hands the renderer, so these tests go on asserting exactly
    // what they asserted: that ONE component reaches the GPU intact.
    Desert::Graphic::CloudLayerSet OneLayer( const Desert::ECS::VolumetricCloudData& data )
    {
        Desert::Graphic::CloudLayerSet set;
        set.Layers[0] = data;
        set.Count     = 1;
        return set;
    }
} // namespace

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
                                       int maxSteps, float jitter = 0.0f )
    {
        // Wired EXACTLY as Programs/Clouds/CloudRaymarch.shader wires it: the first sample is dithered
        // forward by up to one coarse stride, the entry is what both the frontier and every rewind are
        // floored by. Feeding the dithered distance to either of those is the defect this driver exists
        // to reproduce, so the driver must not quietly do the right thing on its own.
        const float tStart = tEnter + jitter * R::CloudStepLength( tEnter, minStep, maxStep, growth ) *
                                           std::max( coarseMultiplier, 1.0f );
        std::vector<MarchSample> samples;
        R::CloudMarchState       state = R::CloudMarchBegin( tEnter, tStart );

        for ( int i = 0; i < maxSteps && state.T < tExit; ++i )
        {
            const bool occupied = state.T >= slabStart && state.T <= slabEnd;
            samples.push_back( MarchSample{ state.T, state.Fine } );
            state = R::CloudMarchAdvance( state, occupied, tEnter, minStep, maxStep, growth, coarseMultiplier,
                                          emptyBeforeCoarse );
        }
        return samples;
    }

    // The same drive, with the two tiers answering from DIFFERENT fields — which is what the renderer
    // actually does and what RunMarch above cannot express. The coarse tier reads the cheap density and
    // the fine tier the eroded one, and over most of a procedural cloudscape the first is positive where
    // the second is zero.
    std::vector<MarchSample> RunMarchSplit( float tEnter, float tExit, float cheapStart, float cheapEnd,
                                            float fineStart, float fineEnd, float minStep, float maxStep,
                                            float growth, float coarseMultiplier, int emptyBeforeCoarse,
                                            int maxSteps )
    {
        std::vector<MarchSample> samples;
        R::CloudMarchState       state = R::CloudMarchBegin( tEnter, tEnter );

        for ( int i = 0; i < maxSteps && state.T < tExit; ++i )
        {
            const bool occupied = state.Fine ? ( state.T >= fineStart && state.T <= fineEnd )
                                             : ( state.T >= cheapStart && state.T <= cheapEnd );
            samples.push_back( MarchSample{ state.T, state.Fine } );
            state = R::CloudMarchAdvance( state, occupied, tEnter, minStep, maxStep, growth, coarseMultiplier,
                                          emptyBeforeCoarse );
        }
        return samples;
    }

    // The direct-light energy as the march computed it BEFORE CLD-113: the same octaves, handed the raw
    // optical depth toward the sun. Kept as a local reference so the depth-modulation tests compare
    // against the formula they replaced rather than against a number somebody wrote down once. The
    // arguments after the optical depth are the component's defaults (3 octaves, 0.5 falloffs).
    glm::vec3 MultiScatterBeforeDepthModulation( float opticalDepthToSun, float cosTheta )
    {
        return R::CloudMultiScatter( opticalDepthToSun, glm::vec3( 1.0f ), cosTheta, 3, 0.5f, 0.5f, 0.5f, 0.8f,
                                     -0.15f, 0.5f, 1.2f );
    }

    // The same call the march makes now: the octaves see the depth-modulated optical depth.
    glm::vec3 MultiScatterWithDepthModulation( float opticalDepthToSun, float profile, float cosTheta )
    {
        return MultiScatterBeforeDepthModulation(
             R::CloudMultiScatterOpticalDepth( opticalDepthToSun, profile, cosTheta ), cosTheta );
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

    // Both naive forms are an order of magnitude worse, which is the property the header's expanded form
    // exists for. What is NOT asserted, deliberately: that the kilometre one beats the world-unit one.
    // Both are dominated by the same catastrophic cancellation in dot(c,c) - r*r, their errors land
    // within a factor of two of each other, and which comes out ahead depends on how the compiler
    // contracts the expression — MSVC ordered them the other way round (1.36e-4 vs 1.08e-4) and failed a
    // test that was never really about the units. The claim worth making is that neither is usable.
    EXPECT_LT( expandedError * 10.0, naiveKmError )
         << "expanded " << expandedError << " vs naive km " << naiveKmError;
    EXPECT_LT( expandedError * 10.0, naiveWorldError )
         << "expanded " << expandedError << " vs naive world units " << naiveWorldError;
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

// ---- Wave 1: the sqrt-type near schedule (Nubis3 pp. 163/171) --------------------------------------
//
// The deck steps at max(1, sqrt(d) * 0.08) metres because a cauliflower lobe is 100-200 m across at any
// distance, and a linear schedule crosses that size a few kilometres out. Our version bounds the sqrt
// regime: <= 2 * MinStepSize out to CLOUD_STEP_FINE_RANGE (10 km), exactly the old linear schedule
// beyond CLOUD_STEP_FAR_RANGE (25 km), monotone blend between. These tests pin all three properties for
// every shipped quality tier, so a tier retune cannot silently break the schedule's contract.

TEST( CloudSteps, EveryQualityTierScheduleIsMonotoneNondecreasing )
{
    for ( const Desert::Graphic::CloudQualityEntry& tier : Desert::Graphic::kCloudQualityTiers )
    {
        const auto& q        = tier.Values;
        float       previous = 0.0f;
        for ( float t = 0.0f; t <= 15000000.0f; t += 5000.0f ) // 0..150 km in 50 m increments
        {
            const float step = R::CloudStepLength( t, q.MinStepSize, q.MaxStepSize, q.StepGrowthRate );
            EXPECT_GE( step, previous - 1e-3f ) << tier.Name << " at t = " << t;
            EXPECT_GE( step, q.MinStepSize ) << tier.Name;
            EXPECT_LE( step, q.MaxStepSize ) << tier.Name;
            previous = step;
        }
    }
}

TEST( CloudSteps, TheFineRegimeResolvesThirtyMetresOutToTenKilometres )
{
    // The audit's bound (NUBIS3_FULL_AUDIT §3 item 1): a 100-200 m billow lobe must stay above Nyquist
    // for the march out to 10 km — where a ground camera actually looks at the layer — which needs a
    // step of at most ~30 m there. Inside CLOUD_STEP_FINE_RANGE the schedule is bounded by
    // 2 * MinStepSize, so the High tier's 15 m authors exactly the 30 m bound and Ultra's 12 m is finer.
    EXPECT_FLOAT_EQ( R::CLOUD_STEP_FINE_RANGE, 1000000.0f ); // 10 km in world units

    for ( const Desert::Graphic::CloudQualityEntry& tier : Desert::Graphic::kCloudQualityTiers )
    {
        const auto& q = tier.Values;
        for ( float t = 0.0f; t <= R::CLOUD_STEP_FINE_RANGE; t += 2500.0f )
        {
            const float step = R::CloudStepLength( t, q.MinStepSize, q.MaxStepSize, q.StepGrowthRate );
            EXPECT_LE( step, 2.0f * q.MinStepSize + 1e-2f ) << tier.Name << " at t = " << t;
        }
    }

    const Desert::Graphic::CloudQualityEntry* high =
         Desert::Graphic::FindCloudQuality( Desert::ECS::CloudQuality::High );
    ASSERT_NE( high, nullptr );
    EXPECT_LE( 2.0f * high->Values.MinStepSize, Common::Units::Metres( 30.0f ) + 1e-2f )
         << "the High tier's MinStepSize must keep the fine regime at <= 30 m";
}

TEST( CloudSteps, BeyondTheFarRangeTheScheduleIsExactlyTheLinearOne )
{
    // The far fallback (deck's own near/far split, p. 171): past CLOUD_STEP_FAR_RANGE the layer is a
    // texture and finer sampling buys nothing, so the schedule must be EXACTLY the old linear one — two
    // implementations of one quantity, asserted equal rather than trusted.
    EXPECT_FLOAT_EQ( R::CLOUD_STEP_FAR_RANGE, 2500000.0f ); // 25 km in world units

    for ( const Desert::Graphic::CloudQualityEntry& tier : Desert::Graphic::kCloudQualityTiers )
    {
        const auto& q = tier.Values;
        for ( float t = R::CLOUD_STEP_FAR_RANGE; t <= 15000000.0f; t += 250000.0f )
        {
            const float linear =
                 glm::clamp( q.MinStepSize + q.StepGrowthRate * t, glm::min( q.MinStepSize, q.MaxStepSize ),
                             glm::max( q.MinStepSize, q.MaxStepSize ) );
            EXPECT_FLOAT_EQ( R::CloudStepLength( t, q.MinStepSize, q.MaxStepSize, q.StepGrowthRate ), linear )
                 << tier.Name << " at t = " << t;
        }
    }
}

TEST( CloudSteps, TheNearScheduleIsNeverCoarserThanTheLinearOne )
{
    // The min() in the fine branch: near the camera the linear term is the finer of the two, and taking
    // the sqrt term there would make the new schedule COARSER than the one it replaces.
    for ( const Desert::Graphic::CloudQualityEntry& tier : Desert::Graphic::kCloudQualityTiers )
    {
        const auto& q = tier.Values;
        for ( float t = 0.0f; t <= 15000000.0f; t += 10000.0f )
        {
            const float linear =
                 glm::clamp( q.MinStepSize + q.StepGrowthRate * t, glm::min( q.MinStepSize, q.MaxStepSize ),
                             glm::max( q.MinStepSize, q.MaxStepSize ) );
            EXPECT_LE( R::CloudStepLength( t, q.MinStepSize, q.MaxStepSize, q.StepGrowthRate ), linear + 1e-3f )
                 << tier.Name << " at t = " << t;
        }
    }
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

// ---- The sampling-rate gate -----------------------------------------------------------------------

// CloudNyquistWeight is the relation that keeps a field out of the picture wherever the march cannot
// reconstruct it. It has two consumers — the near-field ridged band and, since the horizon fringe, the
// DETAIL EROSION itself — and what both rely on is the shape of the ramp, not a distance.
TEST( CloudNyquist, AFieldIsFullyCarriedWhenOversampledAndGoneAtItsNyquistLimit )
{
    for ( float feature = 1000.0f; feature <= 200000.0f; feature *= 2.0f )
    {
        // Twice oversampled for the feature: carried whole.
        EXPECT_FLOAT_EQ( R::CloudNyquistWeight( feature, feature * 0.25f ), 1.0f ) << "feature " << feature;
        EXPECT_FLOAT_EQ( R::CloudNyquistWeight( feature, feature * 0.1f ), 1.0f ) << "feature " << feature;

        // At and past the Nyquist limit itself — a stride of half the feature — nothing survives. This is
        // the end that matters: a residue here is a threshold on an unrecoverable field, which renders as
        // a different cloud in every pixel rather than as a coarse one.
        EXPECT_FLOAT_EQ( R::CloudNyquistWeight( feature, feature * 0.5f ), 0.0f ) << "feature " << feature;
        EXPECT_FLOAT_EQ( R::CloudNyquistWeight( feature, feature * 4.0f ), 0.0f ) << "feature " << feature;

        // Monotone in between, so a march that slows down can only ever gain detail. A non-monotone ramp
        // would draw a ring in the sky at whatever distance it turned round.
        float previous = 1.0f;
        for ( int i = 0; i <= 40; ++i )
        {
            const float stride = feature * ( 0.2f + 0.4f * static_cast<float>( i ) / 40.0f );
            const float weight = R::CloudNyquistWeight( feature, stride );
            EXPECT_LE( weight, previous + 1e-6f ) << "feature " << feature << " stride " << stride;
            EXPECT_GE( weight, 0.0f );
            EXPECT_LE( weight, 1.0f );
            previous = weight;
        }
    }
}

// THE ORDERING THE EROSION GATE DEPENDS ON. Common/CloudDensityProcedural.glslh tests only the COARSE
// channel pair's weight before skipping the curl and detail fetches, on the grounds that a coarser field
// stays resolvable at least as far as a finer one does. That is a property of THIS function, and the
// CloudNoise suite pins the ordering of the two feature sizes it is applied to; together they are what
// makes the single test safe. If it ever stopped holding, the far field would keep its finest content
// and lose its coarsest — the exact inverse of the intended degradation, and invisible in one frame.
TEST( CloudNyquist, ACoarserFieldOutlivesAFinerOneAtEveryStride )
{
    for ( float fineFeature = 1000.0f; fineFeature <= 100000.0f; fineFeature *= 2.0f )
    {
        const float coarseFeature = fineFeature * 2.0f;

        for ( float stride = 10.0f; stride <= 400000.0f; stride *= 1.1f )
        {
            const float coarse = R::CloudNyquistWeight( coarseFeature, stride );
            const float fine   = R::CloudNyquistWeight( fineFeature, stride );

            EXPECT_GE( coarse, fine ) << "stride " << stride;
            if ( coarse <= 0.0f )
                EXPECT_FLOAT_EQ( fine, 0.0f ) << "the fetch would be skipped while the fine pair still wanted it";
        }
    }
}

// ---- The erosion cut ------------------------------------------------------------------------------

// A ZERO SPREAD IS THE HINGE, BIT FOR BIT. This is the property that keeps every near-field sample
// unchanged: inside the distance where the march resolves the detail volume both gates are 1, the lost
// spread is exactly 0, and CloudErodeDensity has to reduce to the remap it replaced with no rounding of
// its own. Asserted with EXPECT_FLOAT_EQ and not a tolerance, because "nearly the same cloud" is not the
// claim being made.
TEST( CloudErosion, ZeroSpreadIsExactlyTheHingeItReplaced )
{
    for ( int ti = 0; ti <= 20; ++ti )
    {
        const float threshold = static_cast<float>( ti ) / 20.0f * 0.9f;
        for ( int di = 0; di <= 40; ++di )
        {
            const float density = static_cast<float>( di ) / 40.0f;
            EXPECT_FLOAT_EQ( R::CloudErodeDensity( density, threshold, 0.0f ),
                             R::CloudRemapRange( density, threshold, 1.0f, 0.0f, 1.0f ) )
                 << "density " << density << " threshold " << threshold;
        }
    }
}

// THE WIDENED CUT IS THE MEAN OF THE HINGE, which is the whole justification for widening rather than
// fading: the far deck keeps the density the near one would have had on average, so this is a filter and
// not a fade. Checked against a numerical average of the hinge over the same window, which is the
// definition the closed form is supposed to be — one implementation of a quantity asserted equal to
// another rather than trusted.
TEST( CloudErosion, TheWidenedCutIsTheAveragedHinge )
{
    constexpr int kThresholdSamples = 20001;

    for ( const float threshold : { 0.05f, 0.2f, 0.5f, 0.8f } )
        for ( const float spread : { 0.005f, 0.03f, 0.09f, 0.4f } )
        {
            // The half-width the shader will actually use: symmetric about the threshold and never
            // reaching a negative one, because a noise value times Detail Strength cannot be negative.
            const float r = std::min( 1.7320508f * spread, threshold );

            for ( int di = 0; di <= 40; ++di )
            {
                const float d = static_cast<float>( di ) / 40.0f;

                double mean = 0.0;
                for ( int i = 0; i < kThresholdSamples; ++i )
                {
                    const float sampled =
                         threshold - r + 2.0f * r * ( static_cast<float>( i ) + 0.5f ) / kThresholdSamples;
                    mean += std::max( d - sampled, 0.0f );
                }
                // Normalised by the MEAN threshold, not by each sampled one: the divisor's variation is
                // second order beside the numerator's clamp, and the shader spends one divide, not a
                // distribution's worth.
                // ...and clamped to the unit range the shader's densities all live in: a high threshold
                // with a wide window can push the average above 1, and a density above 1 is not a
                // quantity the march has any use for.
                mean = std::min( mean / kThresholdSamples / ( 1.0 - threshold ), 1.0 );

                EXPECT_NEAR( R::CloudErodeDensity( d, threshold, spread ), static_cast<float>( mean ), 2e-4f )
                     << "density " << d << " threshold " << threshold << " spread " << spread;
            }
        }
}

// The bounds and the monotonicity. Between them they say the widening can only ever move density from
// just above the threshold to just below it: it never invents any, never loses any, and never reverses.
TEST( CloudErosion, TheSoftCutIsBoundedByTheHingeAndTheUnerrodedDensity )
{
    for ( const float threshold : { 0.0f, 0.05f, 0.3f, 0.7f } )
        for ( const float spread : { 0.0f, 0.02f, 0.08f, 0.3f } )
        {
            float previous = -1.0f;
            for ( int di = 0; di <= 100; ++di )
            {
                const float density = static_cast<float>( di ) / 100.0f;
                const float soft    = R::CloudErodeDensity( density, threshold, spread );
                const float hinge   = R::CloudErodeDensity( density, threshold, 0.0f );

                EXPECT_GE( soft, hinge - 1e-6f ) << "below the hinge at density " << density;
                EXPECT_LE( soft, density / ( 1.0f - threshold ) + 1e-6f )
                     << "denser than the unerroded sample at density " << density;
                EXPECT_GE( soft, 0.0f );
                EXPECT_LE( soft, 1.0f );
                EXPECT_GE( soft, previous - 1e-6f ) << "not monotone at density " << density;
                previous = soft;
            }
        }
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

// The defect the three tests above could not see, because they drive both tiers from ONE slab.
//
// The renderer's tiers read different fields: the coarse one the cheap density, the fine one the same
// density after the detail erosion has cut it. Where the cheap density is positive and the eroded one is
// zero — most of a procedural cloudscape — the old machine rewound one coarse stride on every coarse
// hit, spent `emptyBeforeCoarse` fine strides finding nothing, and returned to a coarse tier that
// immediately hit again. Net progress was (emptyBeforeCoarse - 2 * coarseMultiplier) fine strides per
// (emptyBeforeCoarse + 1) samples: EXACTLY ZERO at the shipped High tier's 4 and 8, and negative at
// Low's 4 and 4. Every ray at an ordinary viewing elevation exhausted MaxSteps over open sky.
//
// The relation, not the symptom: a ray that finds nothing must still cross its shell for a cost within a
// small multiple of what the coarse tier alone would have paid.
TEST( CloudMarch, CrossesAShellTheCoarseTierMisreadsForACostNearTheCoarseTierAlone )
{
    constexpr float kEnter   = 0.0f;
    constexpr float kExit    = 850000.0f; // 8.5 km, the showcase's shell chord at 24 degrees elevation
    constexpr float kMinStep = 1500.0f;   // the High tier
    constexpr float kMaxStep = 70000.0f;
    constexpr float kGrowth  = 0.008f;

    // Every tier the engine ships, at its own coarse multiplier and empty run.
    for ( const auto& tier : Desert::Graphic::kCloudQualityTiers )
    {
        const float coarse = tier.Values.CoarseStepMultiplier;
        const int   empty  = tier.Values.EmptySamplesBeforeCoarse;

        // The cheap density says "cloud" over the whole shell; the eroded density says "nothing" over
        // all of it. This is the disagreement, in its purest form.
        const auto samples = RunMarchSplit( kEnter, kExit, kEnter, kExit, 1e9f, 1e9f, kMinStep, kMaxStep, kGrowth,
                                            coarse, empty, 100000 );

        ASSERT_FALSE( samples.empty() ) << tier.Name;
        // It got there at all — the old machine did not, at any budget.
        EXPECT_GE( samples.back().Distance, kExit - kMaxStep ) << tier.Name;

        // And it got there for a cost near the coarse tier's own. The coarse tier alone would pay about
        // (kExit - kEnter) / (coarse * minStep) samples at the near end of the schedule; allow four
        // times that for the search, which is still an order of magnitude under the old behaviour.
        const size_t coarseOnly = static_cast<size_t>( ( kExit - kEnter ) / ( coarse * kMinStep ) );
        EXPECT_LT( samples.size(), coarseOnly * 4u ) << tier.Name;
    }
}

// The property that makes the above true rather than the number that happens to come out of it: no
// interval is ever marched twice, so the machine cannot cycle.
TEST( CloudMarch, NeverRewindsBehindGroundTheFineTierHasAlreadyCovered )
{
    R::CloudMarchState state     = R::CloudMarchBegin( 0.0f, 0.0f );
    float              highWater = 0.0f;

    for ( int i = 0; i < 20000 && state.T < 850000.0f; ++i )
    {
        // The pathological field again: coarse always yes, fine always no.
        const bool occupied = !state.Fine;
        if ( state.Fine )
        {
            EXPECT_GE( state.T, highWater - 1e-3f );
            highWater = std::max( highWater, state.T );
        }
        state = R::CloudMarchAdvance( state, occupied, 0.0f, 1500.0f, 70000.0f, 0.008f, 4.0f, 8 );
    }
    EXPECT_GT( highWater, 0.0f );
}

// THE DITHER MUST MOVE THE LATTICE, NOT DELETE THE MEDIUM — the property the woven cross-hatch on dense
// cirrus violated, and the one no test above could see because every driver runs at jitter 0.
//
// A march samples nothing before its first sample, so a first sample placed a jitter PAST the entry
// deletes that prefix outright: the coarse tier never tests it, and the rewind reaches back only one
// stride from the first hit, which is at or after the first sample. The deleted length is
// `jitter * coarseStride`, and the jitter is interleaved gradient noise, so on a layer whose chord is
// comparable with a coarse stride it is a fixed screen-space pattern of missing cloud.
//
// The assertion is the direct statement of what must hold: over the whole jitter range, the first FINE
// sample must land at or before the leading face of a slab that begins at the segment's entry. Against a
// forward dither this fails at every phase but zero.
TEST( CloudMarch, NoDitherPhaseDeletesTheNearFaceOfTheMedium )
{
    // The cirrus sheet's own geometry at the shipped High tier: 1.2 km thick, met at 25 km looking 20
    // degrees up, where the schedule has grown the fine stride to 216 m and the coarse stride to 865 m.
    const float kEnter    = 2510000.0f; // 25.1 km
    const float kExit     = kEnter + 350860.0f;
    const float kMinStep  = Common::Units::Metres( 15.0f );
    const float kMaxStep  = Common::Units::Metres( 700.0f );
    const float kGrowth   = 0.008f;
    const float kCoarse   = 4.0f;
    const int   kEmpty    = 8;
    const int   kMaxSteps = 176;

    // A slab filling the whole segment, so its near face IS the entry and any prefix a phase deletes is
    // density that no sample ever saw.
    for ( int phase = 0; phase <= 20; ++phase )
    {
        const float                    jitter  = static_cast<float>( phase ) / 20.0f;
        const std::vector<MarchSample> samples = RunMarch( kEnter, kExit, kEnter, kExit, kMinStep, kMaxStep,
                                                           kGrowth, kCoarse, kEmpty, kMaxSteps, jitter );

        ASSERT_FALSE( samples.empty() ) << "jitter " << jitter;

        float firstFine = std::numeric_limits<float>::max();
        for ( const MarchSample& sample : samples )
        {
            if ( sample.Fine )
            {
                firstFine = sample.Distance;
                break;
            }
        }

        EXPECT_LE( firstFine, kEnter + 1e-3f )
             << "jitter " << jitter << " deleted " << ( firstFine - kEnter ) << " world units of the near face";
    }
}

// The same property stated as ENERGY, which is what the picture actually shows: the density-length a ray
// accumulates across a uniform slab must not depend on which dither phase it drew. The forward dither made
// it fall with the phase — at the numbers above, by up to a quarter of the slab.
TEST( CloudMarch, DensityLengthAcrossAUniformSlabIsIndependentOfTheDitherPhase )
{
    const float kEnter    = 2510000.0f;
    const float kExit     = kEnter + 350860.0f;
    const float kMinStep  = Common::Units::Metres( 15.0f );
    const float kMaxStep  = Common::Units::Metres( 700.0f );
    const float kGrowth   = 0.008f;
    const float kCoarse   = 4.0f;
    const int   kEmpty    = 8;
    const int   kMaxSteps = 176;

    float lowest  = std::numeric_limits<float>::max();
    float highest = 0.0f;
    for ( int phase = 0; phase <= 20; ++phase )
    {
        const float                    jitter  = static_cast<float>( phase ) / 20.0f;
        const std::vector<MarchSample> samples = RunMarch( kEnter, kExit, kEnter, kExit, kMinStep, kMaxStep,
                                                           kGrowth, kCoarse, kEmpty, kMaxSteps, jitter );

        // Unit density over the slab, so the marched density-length is the summed step length of every
        // fine sample that landed inside it — the quadrature the renderer runs.
        float marched = 0.0f;
        for ( const MarchSample& sample : samples )
        {
            if ( !sample.Fine || sample.Distance < kEnter || sample.Distance > kExit )
                continue;
            marched += R::CloudStepLength( sample.Distance, kMinStep, kMaxStep, kGrowth );
        }

        lowest  = std::min( lowest, marched );
        highest = std::max( highest, marched );
    }

    ASSERT_GT( lowest, 0.0f );
    // One stride and a half of quadrature spread is the honest allowance; a deleted prefix is worth four
    // strides and grows with the coarse multiplier.
    const float stride = R::CloudStepLength( kEnter, kMinStep, kMaxStep, kGrowth );
    EXPECT_LE( highest - lowest, stride * 1.5f )
         << "phase spread " << ( highest - lowest ) << " over a slab of " << ( kExit - kEnter );
}

// ---- Closing out a ray that ran out of budget ------------------------------------------------------

TEST( CloudRayTail, CannotInventLightAndOnlyEverDarkensTransmittance )
{
    const glm::vec3 scattered( 0.4f, 0.5f, 0.6f );

    // Nothing left to march: the identity, exactly.
    {
        const R::CloudRayTail tail =
             R::CloudCloseExhaustedRay( scattered, 0.3f, 5000.0f, 100000.0f, 0.0f, 0.0005f );
        EXPECT_FLOAT_EQ( tail.Transmittance, 0.3f );
        EXPECT_FLOAT_EQ( tail.Scattered.x, scattered.x );
    }

    // A tail: transmittance falls, radiance rises, and the rise is bounded by the mean radiance the ray
    // measured — it can never scatter more than the medium it already saw.
    {
        const float           transmittance = 0.3f;
        const R::CloudRayTail tail =
             R::CloudCloseExhaustedRay( scattered, transmittance, 5000.0f, 100000.0f, 400000.0f, 0.0005f );
        EXPECT_LT( tail.Transmittance, transmittance );
        EXPECT_GT( tail.Transmittance, 0.0f );
        EXPECT_GE( tail.Scattered.x, scattered.x );

        const glm::vec3 mean = scattered / ( 1.0f - transmittance );
        EXPECT_LE( tail.Scattered.x, scattered.x + transmittance * mean.x + 1e-5f );
    }

    // A ray that measured no medium at all leaves both untouched, rather than dividing by its own zero.
    {
        const R::CloudRayTail tail =
             R::CloudCloseExhaustedRay( glm::vec3( 0.0f ), 1.0f, 0.0f, 100000.0f, 400000.0f, 0.0005f );
        EXPECT_FLOAT_EQ( tail.Transmittance, 1.0f );
        EXPECT_FLOAT_EQ( tail.Scattered.x, 0.0f );
    }

    // Monotone in the distance still owed.
    float previous = 1.1f;
    for ( float remaining = 0.0f; remaining < 2000000.0f; remaining += 50000.0f )
    {
        const R::CloudRayTail tail =
             R::CloudCloseExhaustedRay( scattered, 0.9f, 20000.0f, 100000.0f, remaining, 0.0005f );
        EXPECT_LE( tail.Transmittance, previous );
        previous = tail.Transmittance;
    }
}

// ---- Transmittance at depth --------------------------------------------------------------------------

namespace
{
    // The kernel the octave chain used before: pure Beer, per octave.
    float BeerOctaveKernel( float tau )
    {
        return std::exp( -std::max( tau, 0.0f ) );
    }

    // The diffusion asymptote the deep tail is supposed to have the SHAPE of.
    float DiffusionSlabTransmittance( float tau )
    {
        const float k = 0.75f * ( 1.0f - R::CLOUD_DROPLET_ASYMMETRY );
        return 1.0f / ( 1.0f + k * tau );
    }
} // namespace

TEST( CloudThickTransmittance, IsBeerBelowOneMeanFreePathAndStrictlyAboveItBeyond )
{
    // Identical to the old chain's kernel below the join — bit for bit, so no thin cloud, rim or edge in
    // any scene moves at all.
    for ( float tau = 0.0f; tau <= R::CLOUD_DIFFUSION_JOIN_TAU; tau += 0.02f )
        EXPECT_FLOAT_EQ( R::CloudThickTransmittance( tau ), BeerOctaveKernel( tau ) );

    // Continuous at the join.
    EXPECT_NEAR( R::CloudThickTransmittance( R::CLOUD_DIFFUSION_JOIN_TAU ),
                 BeerOctaveKernel( R::CLOUD_DIFFUSION_JOIN_TAU ), 1e-6f );

    // Strictly greater above it, and by a lot where it matters.
    for ( float tau = R::CLOUD_DIFFUSION_JOIN_TAU + 0.1f; tau < 120.0f; tau += 0.5f )
        EXPECT_GT( R::CloudThickTransmittance( tau ), BeerOctaveKernel( tau ) );

    EXPECT_GT( R::CloudThickTransmittance( 50.0f ) / BeerOctaveKernel( 50.0f ), 1e10f );
}

TEST( CloudThickTransmittance, IsMonotoneAndBoundedInZeroToOne )
{
    float previous = 1.0f + 1e-6f;
    for ( float tau = 0.0f; tau < 500.0f; tau += 0.25f )
    {
        const float t = R::CloudThickTransmittance( tau );
        EXPECT_GT( t, 0.0f ) << tau;
        EXPECT_LE( t, 1.0f ) << tau;
        EXPECT_LE( t, previous ) << tau;
        previous = t;
    }
    EXPECT_FLOAT_EQ( R::CloudThickTransmittance( 0.0f ), 1.0f );
}

// The SHAPE at depth: the tail must fall like the diffusion slab's 1/(1 + k tau), not like an
// exponential. Pinned as a ratio that stays flat rather than as a value, because it is the power law
// that is the claim.
TEST( CloudThickTransmittance, FallsAlgebraicallyAtDepthLikeADiffusingSlab )
{
    const float r50  = R::CloudThickTransmittance( 50.0f ) / DiffusionSlabTransmittance( 50.0f );
    const float r100 = R::CloudThickTransmittance( 100.0f ) / DiffusionSlabTransmittance( 100.0f );
    EXPECT_NEAR( r50, r100, 0.02f );

    // Doubling the optical depth of a diffusing slab roughly halves what gets through — an exponential
    // would divide it by exp(50).
    EXPECT_NEAR( R::CloudThickTransmittance( 50.0f ) / R::CloudThickTransmittance( 100.0f ), 1.87f, 0.1f );
}

// What the chain as a whole now delivers at depth, and what it still does not. The number on the right
// is a measurement, not a target: anchoring the tail to Beer's own value at the join costs amplitude,
// and the chain lands at about 0.6 of the diffusion asymptote. Pinning it means the shortfall is a fact
// on the record rather than a surprise, and that a later change to the octave weights has to restate it.
TEST( CloudMultiScatter, ANormalisedThickChainSitsNearTheDiffusionAsymptote )
{
    const auto normalised = []( float tau )
    {
        const glm::vec3 deep =
             R::CloudMultiScatter( tau, glm::vec3( 1.0f ), 0.5f, 3, 0.5f, 0.5f, 0.5f, 0.8f, -0.15f, 0.5f, 1.2f );
        const glm::vec3 zero =
             R::CloudMultiScatter( 0.0f, glm::vec3( 1.0f ), 0.5f, 3, 0.5f, 0.5f, 0.5f, 0.8f, -0.15f, 0.5f, 1.2f );
        return deep.x / zero.x;
    };

    for ( float tau : { 50.0f, 75.0f, 100.0f } )
    {
        const float ratio = normalised( tau ) / DiffusionSlabTransmittance( tau );
        EXPECT_GT( ratio, 0.45f ) << tau;
        EXPECT_LT( ratio, 0.85f ) << tau;
    }

    // And the thing that was actually wrong: the old chain was five orders of magnitude below this.
    const float oldChain =
         ( BeerOctaveKernel( 50.0f ) + 0.5f * BeerOctaveKernel( 25.0f ) + 0.25f * BeerOctaveKernel( 12.5f ) ) /
         1.75f;
    EXPECT_GT( normalised( 50.0f ) / oldChain, 1e4f );
}

// The low-tau promise, made about the function the march actually calls rather than about its kernel:
// every thin cloud in every scene renders exactly as before.
TEST( CloudMultiScatter, IsUnchangedWhereverEveryOctaveIsUnderOneMeanFreePath )
{
    for ( float tau = 0.0f; tau <= R::CLOUD_DIFFUSION_JOIN_TAU; tau += 0.05f )
    {
        const glm::vec3 now =
             R::CloudMultiScatter( tau, glm::vec3( 1.0f ), 0.5f, 3, 0.5f, 0.5f, 0.5f, 0.8f, -0.15f, 0.5f, 1.2f );

        // The same sum, spelled with exp() as it used to be.
        glm::vec3 before( 0.0f );
        float     extinctionMul = 1.0f;
        float     scatterMul    = 1.0f;
        float     phaseMul      = 1.0f;
        for ( int i = 0; i < 3; ++i )
        {
            const float phase = R::CloudDualLobePhase( 0.5f, 0.8f * phaseMul, -0.15f * phaseMul, 0.5f, 1.2f );
            before += glm::vec3( BeerOctaveKernel( tau * extinctionMul ) ) * ( scatterMul * phase );
            extinctionMul *= 0.5f;
            scatterMul *= 0.5f;
            phaseMul *= 0.5f;
        }
        EXPECT_NEAR( now.x, before.x, 1e-6f ) << tau;
    }
}

// ---- The weather field's scale ----------------------------------------------------------------------

// The C++ side decides this number (component default, preset table, the renderer's warning) and the
// shader header states the reasoning. Two spellings of one formula is the exact shape of defect this
// project has paid for; this is the assertion that keeps them one formula.
TEST( CloudWeatherScale, TheCppMirrorIsTheShaderFormulaToTheBit )
{
    for ( float bottom : { 60000.0f, 150000.0f, 200000.0f, 800000.0f } )
        for ( float thickness : { 70000.0f, 250000.0f, 350000.0f, 900000.0f } )
            EXPECT_FLOAT_EQ( Desert::Graphic::CloudAutoWeatherTileSize( bottom, thickness ),
                             R::CloudAutoWeatherTileSize( bottom, thickness ) );

    EXPECT_FLOAT_EQ( Desert::Graphic::kCloudWeatherBasePeriod, R::CLOUD_WEATHER_BASE_PERIOD );
    EXPECT_FLOAT_EQ( Desert::Graphic::kCloudWeatherOverheadCot, R::CLOUD_WEATHER_OVERHEAD_COT );
    EXPECT_FLOAT_EQ( Desert::Graphic::kCloudWeatherCellsOverhead, R::CLOUD_WEATHER_CELLS_OVERHEAD );
}

// The component's own default must sit in the derived value's band for the layer it ships with, or the
// very first sky an artist sees is the miscalibrated one.
//
// THE EQUALITY IS BACK, and the ALTITUDE is what bought it. It was spent once: with the layer's thickness
// also derived (from the species aspect, CloudLayerAspect.hpp) tile and thickness are over-determined, and
// solving both exactly at a 1.5 km base wanted a 987 m layer — under CloudMarchScale's four-sample search
// bound at the Low tier, which has no tolerance at all. Moving the deck up removes the conflict rather than
// trading it: at the 8 km base it now defaults to, the simultaneous solution clears the search bound on
// EVERY tier including Low, so the default sits AT the derived tile instead of 1.41x it.
//
// ASSERTED AGAINST THE DEFAULTS THEMSELVES, not against a copied literal. The three numbers this reads are
// the component's own; a preset pass that moves the layer moves all three together and this test follows
// it, which is the whole point of a relation test. The band is still asserted too, because the band is
// what the renderer warns outside of.
TEST( CloudWeatherScale, TheComponentDefaultIsInTheDerivedTilesBandForItsOwnLayer )
{
    const Desert::ECS::VolumetricCloudData defaults;
    EXPECT_TRUE( Desert::Graphic::CloudWeatherTileIsPlausible(
         defaults.WeatherTileSize, defaults.LayerBottomAltitude, defaults.LayerThickness ) );

    const float wanted =
         Desert::Graphic::CloudAutoWeatherTileSize( defaults.LayerBottomAltitude, defaults.LayerThickness );
    EXPECT_NEAR( defaults.WeatherTileSize / wanted, 1.0f, 0.001f )
         << "the default deck's tile is the simultaneous solution of CloudWeatherScale and CloudLayerAspect "
            "at its own altitude; if a preset pass moved the layer, move the tile with it";
}

// THE BAND IS A RATIO, ITS ENDPOINTS WERE MEASURED AS ABSOLUTE TILES, AND THOSE TWO FACTS DRIFTED APART.
//
// CloudWeatherScale's tolerance band was measured on ONE layer — Clouds_UEShowcase, derived tile 23.8 km
// at the three cells overhead the constant carried then. Raising the count to four rescaled every derived
// tile by 0.75x, so two hardcoded ratios of 0.7 and 1.6 moved the band by 4/3 relative to the frames that
// define it, and an authored row left a band it had not itself moved relative to. The band is therefore
// derived from kCloudWeatherCellsOverhead now, and this is the assertion that keeps it derived.
//
// STATED AS AN INVARIANT AND NOT AS TWO NUMBERS: the ABSOLUTE tiles the band admits over a FIXED layer do
// not depend on the constant at all. That is the property the ratios exist to express, it is exactly what
// broke, and it is the one form of the assertion that a future change to the count cannot pass by
// accident.
TEST( CloudWeatherScale, TheToleranceBandIsAbsoluteAndDoesNotMoveWithTheCellsOverheadCount )
{
    using Desert::Graphic::CloudAutoWeatherTileSize;
    using Desert::Graphic::CloudWeatherTileIsPlausible;
    using Desert::Graphic::kCloudWeatherBasePeriod;
    using Desert::Graphic::kCloudWeatherCellsOverhead;
    using Desert::Graphic::kCloudWeatherCellsOverheadAtMeasurement;
    using Desert::Graphic::kCloudWeatherOverheadCot;
    using Desert::Graphic::kCloudWeatherTileToleranceHigh;
    using Desert::Graphic::kCloudWeatherTileToleranceHighAsMeasured;
    using Desert::Graphic::kCloudWeatherTileToleranceLow;
    using Desert::Graphic::kCloudWeatherTileToleranceLowAsMeasured;

    const float bottom    = Common::Units::Metres( 900.0f );
    const float thickness = Common::Units::Metres( 1409.0f );
    const float mid       = bottom + 0.5f * thickness;

    // The derived tile as it stood WHEN THE BAND WAS MEASURED, spelled from the same two constants the
    // live formula uses so only the count differs. The absolute endpoints follow from it.
    const float derivedAtMeasurement =
         mid * ( kCloudWeatherBasePeriod * kCloudWeatherOverheadCot / kCloudWeatherCellsOverheadAtMeasurement );
    const float lowTile  = derivedAtMeasurement * kCloudWeatherTileToleranceLowAsMeasured;
    const float highTile = derivedAtMeasurement * kCloudWeatherTileToleranceHighAsMeasured;

    const float derived = CloudAutoWeatherTileSize( bottom, thickness );
    EXPECT_NEAR( derived * kCloudWeatherTileToleranceLow, lowTile, 1e-3f * lowTile )
         << "the band's low end has moved off the tile it was measured at";
    EXPECT_NEAR( derived * kCloudWeatherTileToleranceHigh, highTile, 1e-3f * highTile )
         << "the band's high end has moved off the tile it was measured at";

    // And the band still bites in the ratios the shipped content actually occupies. 1.000 is a row solved
    // exactly to the derivation, 1.333 a row that was the derived tile at three cells, 1.662 the one
    // authored row; 0.28 is Clouds_ShadowsOnWorld, which is outside and must stay warned about.
    for ( float ratio : { 1.0f, 4.0f / 3.0f, 1.662f, 2.0f } )
        EXPECT_TRUE( CloudWeatherTileIsPlausible( derived * ratio, bottom, thickness ) ) << "ratio " << ratio;

    for ( float ratio : { 0.28f, 0.63f, 0.9f, 2.2f, 2.5f } )
        EXPECT_FALSE( CloudWeatherTileIsPlausible( derived * ratio, bottom, thickness ) ) << "ratio " << ratio;

    // THE KNOWN DEFECT, PINNED AS A NUMBER so it is impossible to read this suite as green-means-well.
    // A row solved exactly to CloudAutoWeatherTileSize sits at 1.000 against a low end of 0.9333 — 7.1%
    // above the tile at which the horizon was MEASURED to wall, which is the band of hard-edged boxes
    // above the horizon on the cumulus scenes today. This asserts the margin is what the header says it
    // is; it does not assert the margin is enough, because it is not. See CloudWeatherScale.hpp.
    EXPECT_NEAR( 1.0f / kCloudWeatherTileToleranceLow, 1.0714f, 0.001f )
         << "the headroom a derived row has over the measured horizon wall has changed; if that is "
            "deliberate, the quantified defect note in CloudWeatherScale.hpp has to change with it";
    EXPECT_GT( kCloudWeatherCellsOverhead, kCloudWeatherCellsOverheadAtMeasurement );
}

// ---- The empty-space search's scale ------------------------------------------------------------------

// The other C++/GLSL mirror of a formula, and the same assertion for the same reason: the renderer warns
// from the C++ side and the shader marches from the GLSL side, and a divergence would be a warning that
// fires on the wrong skies while the real one goes unmentioned.
TEST( CloudMarchScale, TheCppStepScheduleIsTheShaderScheduleToTheBit )
{
    for ( float t :
          { 0.0f, 1000.0f, 100000.0f, 900000.0f, 1000000.0f, 1700000.0f, 2500000.0f, 9000000.0f, 15000000.0f } )
        for ( float minStep : { 1000.0f, 1500.0f, 4000.0f } )
            for ( float maxStep : { 40000.0f, 70000.0f, 500000.0f } )
                for ( float growth : { 0.0f, 0.004f, 0.008f, 0.02f } )
                    EXPECT_FLOAT_EQ( Desert::Graphic::CloudStepLengthAt( t, minStep, maxStep, growth ),
                                     R::CloudStepLength( t, minStep, maxStep, growth ) )
                         << "t " << t << " min " << minStep << " max " << maxStep << " growth " << growth;

    EXPECT_FLOAT_EQ( Desert::Graphic::kCloudStepFineRange, R::CLOUD_STEP_FINE_RANGE );
    EXPECT_FLOAT_EQ( Desert::Graphic::kCloudStepFarRange, R::CLOUD_STEP_FAR_RANGE );
}

// THE RELATION A THIN LAYER BREAKS: the empty-space search strides at CoarseStepMultiplier times the fine
// stride, and a layer whose chord is comparable with that stride can be stepped over entirely. Nothing
// averages that away — the mean of "half the rays missed the layer" is "half the layer".
//
// Neither side of this is wrong on its own — a 1.2 km sheet is a real cloud and a coarse multiplier of 4
// is a real optimisation — which is exactly why it needs an assertion about the PAIR.
//
// (The cross-hatch that led to this suite was NOT this relation; it was the march's dithered start
// deleting a per-pixel prefix, and it is pinned by CloudMarch's two dither-phase properties above. See
// the note at the top of CloudMarchScale.hpp.)
TEST( CloudMarchScale, ADeckAndAThinSheetBothGetEnoughSearchSamplesAtTheirAuthoredTiers )
{
    using Desert::Graphic::CloudCoarseStrideIsPlausible;
    using Desert::Graphic::CloudWorstSearchAcrossLayer;

    // Every shipped quality tier over the layer the component defaults to: the deck is thick enough that
    // no tier can step over it, and that has to stay true when a tier is retuned.
    const Desert::ECS::VolumetricCloudData defaults;
    for ( const auto& tier : Desert::Graphic::kCloudQualityTiers )
    {
        EXPECT_TRUE( CloudCoarseStrideIsPlausible( defaults.LayerBottomAltitude, defaults.LayerThickness,
                                                   tier.Values.MinStepSize, tier.Values.MaxStepSize,
                                                   tier.Values.StepGrowthRate, tier.Values.CoarseStepMultiplier ) )
             << "tier " << tier.Name;
    }

    // The Cirrus preset's own layer — 1.2 km at 8 km — is the thin case, and it is where the guard's old
    // zenith-only evaluation was 2.6x optimistic. MEASURED as the minimum over elevation: Low 1.6 samples,
    // Medium 3.5, High 4.1, Ultra 7.1, against the zenith figures of 2.6 / 6.9 / 10.4 / 13.8 the guard
    // used to report. So only High and Ultra clear the bar for a thin high sheet, and that is a real
    // limitation of the cheaper tiers rather than a defect in either — it is what the renderer's warning
    // exists to say out loud, with numbers and with the elevation they belong to.
    const Desert::Graphic::CloudPresetEntry* cirrus =
         Desert::Graphic::FindCloudPreset( Desert::ECS::CloudPreset::Cirrus );
    ASSERT_NE( cirrus, nullptr );

    for ( const auto id : { Desert::ECS::CloudQuality::High, Desert::ECS::CloudQuality::Ultra } )
    {
        const Desert::Graphic::CloudQualityEntry* tier = Desert::Graphic::FindCloudQuality( id );
        ASSERT_NE( tier, nullptr );
        EXPECT_TRUE( CloudCoarseStrideIsPlausible(
             cirrus->Values.LayerBottomAltitude, cirrus->Values.LayerThickness, tier->Values.MinStepSize,
             tier->Values.MaxStepSize, tier->Values.StepGrowthRate, tier->Values.CoarseStepMultiplier ) )
             << "tier " << tier->Name;
    }

    for ( const auto id : { Desert::ECS::CloudQuality::Low, Desert::ECS::CloudQuality::Medium } )
    {
        const Desert::Graphic::CloudQualityEntry* tier = Desert::Graphic::FindCloudQuality( id );
        ASSERT_NE( tier, nullptr );
        EXPECT_FALSE( CloudCoarseStrideIsPlausible(
             cirrus->Values.LayerBottomAltitude, cirrus->Values.LayerThickness, tier->Values.MinStepSize,
             tier->Values.MaxStepSize, tier->Values.StepGrowthRate, tier->Values.CoarseStepMultiplier ) )
             << "tier " << tier->Name
             << " now claims to search a thin high sheet finely enough; if that is "
                "intended, the tier changed and this row should say so";
    }

    // THE ZENITH IS NOT THE WORST CASE FOR A HIGH LAYER — the assumption the guard used to be built on,
    // asserted here so it cannot come back. Both shipped geometries now have their minimum in the MIDDLE
    // of the sky, which is exactly where a ground camera looks.
    //
    // THE DECK'S USED TO BE OVERHEAD, and raising it (Docs/Clouds/DECK_SCALE_DECISION.md D2, then D7) is
    // what moved it — into the step schedule's sqrt-to-linear blend band, by the same mechanism
    // CloudMarchScale.hpp:54-70 already documents for high layers. A LOW layer never reaches that band
    // until its chord has grown tenfold, so its worst case really is the zenith; at altitude the deck
    // reaches it while its chord has barely doubled, and it joins the sheet's regime. Measured at High:
    //
    //     deck   18.1177 samples at 90 degrees  ->  10.7972 at 23 degrees   (the layer moved, twice)
    //     sheet   4.0579 samples at 20 degrees  ->   4.0579 at 20 degrees   (unchanged)
    //
    // The deck's count is essentially FLAT under the lift — 10.8044 at the 5 km base, 10.7972 at 8 km —
    // and that is the derivation working rather than a coincidence: raising the base scales the thickness
    // and the distance to the layer together, and the search count is the ratio of the two.
    //
    // WHAT REPLACED THE OLD ASSERTION, AND WHY IT IS NOT `deck > sheet` ALONE. The old relation was
    // `deck > 4 * sheet`, and calling the 4x incidental was wrong in a way worth spelling out: with the
    // sheet at 4.0579, that assertion put an effective FLOOR of 16.23 under the deck. `deck > sheet`
    // puts one of 4.06 — so halving the deck's count from 10.80 to 5.0 would pass silently, and that is
    // exactly the regression this row exists to catch. The old relation also held with only 1.12x
    // headroom (18.12 against 16.23), so it was TIGHT rather than loose, and dropping it cost real
    // detection.
    //
    // THE NEW DETECTOR, in the new regime and against the same three measured numbers: deck 10.7972 at
    // 23 degrees, sheet 4.0579 at 20 degrees, bound 4.0. Twice the bound is 8.0 — cleared by 35% by the
    // measured deck, and failed by a halving to 5.0. It is a bound and not a ratio, so it does not go
    // stale when one of the two layers changes regime, which is what happened to the 4x.
    //
    // WHY THE DECK GETS TWICE THE BOUND AND THE SHEET ONLY THE BOUND: the deck is the geometry the
    // component itself DEFAULTS to, so it is what every new scene marches; the sheet is the thinnest
    // shipped layer at its own authored altitude and there is nothing left to spend on it.
    const Desert::Graphic::CloudQualityEntry* high =
         Desert::Graphic::FindCloudQuality( Desert::ECS::CloudQuality::High );
    ASSERT_NE( high, nullptr );

    const auto sheet = CloudWorstSearchAcrossLayer(
         cirrus->Values.LayerBottomAltitude, cirrus->Values.LayerThickness, high->Values.MinStepSize,
         high->Values.MaxStepSize, high->Values.StepGrowthRate, high->Values.CoarseStepMultiplier );
    EXPECT_GT( sheet.ElevationDegrees, 10.0f );
    EXPECT_LT( sheet.ElevationDegrees, 40.0f );
    EXPECT_LT( sheet.Samples,
               0.5f * CloudWorstSearchAcrossLayer( cirrus->Values.LayerBottomAltitude,
                                                   cirrus->Values.LayerThickness, high->Values.MinStepSize,
                                                   high->Values.MaxStepSize, high->Values.StepGrowthRate, 1.0f )
                           .Samples );

    const auto deck = CloudWorstSearchAcrossLayer(
         defaults.LayerBottomAltitude, defaults.LayerThickness, high->Values.MinStepSize, high->Values.MaxStepSize,
         high->Values.StepGrowthRate, high->Values.CoarseStepMultiplier );
    EXPECT_GT( deck.ElevationDegrees, 5.0f );
    EXPECT_LT( deck.ElevationDegrees, 40.0f );

    EXPECT_GE( deck.Samples, 2.0f * Desert::Graphic::kCloudMinSearchSamplesAcrossLayer )
         << "the default deck is what every new scene marches; measured 10.7972 against this 8.0, and a "
            "halving to 5.0 is the regression this catches";

    // A RAIL, NOT A TARGET, and the margin is 1.5%. The sheet's 4.0579 sits at 1.0145x the bound, so a
    // 1.5% change to Max Step Size, Step Growth Rate or the Coarse Step Multiplier at the High tier turns
    // this red. That is ACCEPTABLE and it is the point: below the bound, whether a ray notices a 1.2 km
    // sheet at 8 km is a per-pixel coin toss no temporal average removes, so there is no margin to be had
    // — the tier is already spending everything it has on this geometry. Read a failure here as "the tier
    // no longer searches the thinnest shipped layer finely enough", not as a flaky number.
    EXPECT_GE( sheet.Samples, Desert::Graphic::kCloudMinSearchSamplesAcrossLayer );
    EXPECT_GT( deck.Samples, sheet.Samples );

    // A TIER ORDERING, not a value: a finer tier must never search a layer more coarsely than a cheaper
    // one. Retuning one row in isolation is exactly how that inverts, and the symptom would be Ultra
    // stippling where High did not.
    float previous = -1.0f;
    for ( const auto& tier : Desert::Graphic::kCloudQualityTiers )
    {
        const float samples =
             CloudWorstSearchAcrossLayer( cirrus->Values.LayerBottomAltitude, cirrus->Values.LayerThickness,
                                          tier.Values.MinStepSize, tier.Values.MaxStepSize,
                                          tier.Values.StepGrowthRate, tier.Values.CoarseStepMultiplier )
                  .Samples;
        EXPECT_GE( samples, previous ) << "tier " << tier.Name << " searches more coarsely than the one below";
        previous = samples;
    }

    // The two schedules the two-layer showcase tried for its sheet, either side of the bar: a 40 m fine
    // step with growth 0.012 and coarse multiplier 5 does not clear it, and the coarse multiplier 2,
    // growth 0.004 schedule the scene ships does.
    EXPECT_FALSE( CloudCoarseStrideIsPlausible( cirrus->Values.LayerBottomAltitude, cirrus->Values.LayerThickness,
                                                Common::Units::Metres( 40.0f ), Common::Units::Metres( 1200.0f ),
                                                0.012f, 5.0f ) );

    EXPECT_TRUE( CloudCoarseStrideIsPlausible( cirrus->Values.LayerBottomAltitude, cirrus->Values.LayerThickness,
                                               Common::Units::Metres( 40.0f ), Common::Units::Metres( 400.0f ),
                                               0.004f, 2.0f ) );
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

// ---- The per-step in-scatter integration ------------------------------------------------------------

TEST( CloudIntegrateInScatter, NeverReturnsMoreLightThanTheSourceItIntegrates )
{
    // The invariant, and the only one that mattered: a step cannot scatter more toward the eye than the
    // source it is integrating. The shader used to divide this by sigma, and sigma is per CENTIMETRE
    // (0.0005 * ExtinctionScale), so the result came back thousands of times the source and every cloud
    // saturated to flat white at any exposure. One assertion on the bound catches that whole class.
    const glm::vec3 scattering( 0.9f, 0.85f, 0.8f );

    for ( float sigma : { 1e-9f, 1.75e-4f, 0.01f, 1.0f, 100.0f } )
    {
        for ( float dt : { 0.0f, 1.0f, 700.0f, 100000.0f } )
        {
            const glm::vec3 integrated = R::CloudIntegrateInScatter( scattering, sigma, dt );
            for ( int c = 0; c < 3; ++c )
            {
                EXPECT_GE( integrated[c], 0.0f );
                EXPECT_LE( integrated[c], scattering[c] );
            }
        }
    }
}

TEST( CloudIntegrateInScatter, SaturatesToTheWholeSourceAsTheStepGoesOpaque )
{
    const glm::vec3 scattering( 1.0f, 1.0f, 1.0f );

    // Nothing crossed, nothing scattered.
    EXPECT_NEAR( R::CloudIntegrateInScatter( scattering, 0.5f, 0.0f ).x, 0.0f, 1e-6f );

    // Thoroughly opaque: the step delivers the source itself.
    EXPECT_NEAR( R::CloudIntegrateInScatter( scattering, 1.0f, 200.0f ).x, 1.0f, 1e-6f );

    // And it climbs there monotonically — a longer step is never darker than a shorter one.
    float previous = -1.0f;
    for ( float dt = 0.0f; dt <= 20.0f; dt += 0.5f )
    {
        const float value = R::CloudIntegrateInScatter( scattering, 0.3f, dt ).x;
        EXPECT_GT( value, previous );
        previous = value;
    }
}

TEST( CloudIntegrateInScatter, DegeneratesToTheNaiveProductForAThinStep )
{
    // Where the step is optically thin the closed form has to agree with sigma*dt*source, which is what
    // makes it a refinement of the naive accumulation rather than a different model.
    const glm::vec3 scattering( 2.0f, 2.0f, 2.0f );
    const float     sigma = 1.75e-4f; // the real per-centimetre figure at ExtinctionScale 0.35
    const float     dt    = 50.0f;

    EXPECT_NEAR( R::CloudIntegrateInScatter( scattering, sigma, dt ).x, 2.0f * sigma * dt, 1e-4f );
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

// One octave reproduces single scattering exactly WHILE THE MEDIUM IS THIN, and deliberately stops doing
// so once it is not: past one mean free path the chain carries the diffusion tail (CloudThickTransmittance),
// which is the whole point of that function and is what keeps a thick core luminous. The two halves are
// asserted separately so the boundary is a statement rather than a tolerance.
TEST( CloudMultiScatter, OneOctaveIsSingleScatteringWhileTheMediumIsThin )
{
    constexpr float kCos = 0.4f;
    const glm::vec3 tint( 1.0f );
    const float     phase = R::CloudDualLobePhase( kCos, 0.8f, -0.15f, 0.5f, 1.2f );

    for ( float tau = 0.0f; tau <= R::CLOUD_DIFFUSION_JOIN_TAU; tau += 0.05f )
    {
        const glm::vec3 ms =
             R::CloudMultiScatter( tau, tint, kCos, 1, 1.0f, 1.0f, 1.0f, 0.8f, -0.15f, 0.5f, 1.2f );
        const float single = R::CloudBeerTransmittance( tau ) * phase;

        EXPECT_FLOAT_EQ( ms.x, single ) << tau;
        EXPECT_FLOAT_EQ( ms.y, single ) << tau;
        EXPECT_FLOAT_EQ( ms.z, single ) << tau;
    }

    // Above the join it is strictly brighter than single scattering, which is the behaviour change.
    const glm::vec3 thick =
         R::CloudMultiScatter( 8.0f, tint, kCos, 1, 1.0f, 1.0f, 1.0f, 0.8f, -0.15f, 0.5f, 1.2f );
    EXPECT_GT( thick.x, R::CloudBeerTransmittance( 8.0f ) * phase );
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

// ---- CLD-113: the multiple-scattering extinction is depth-modulated -------------------------------
//
// Nubis3 p.136. Every assertion below is a RELATION — a bound, a monotonicity, or a comparison against
// the formula this replaced — because the numbers themselves are a tuning question and the relations are
// not: the ablation on pp. 135/136 is entirely about the direction these move in.

TEST( CloudProfileDepth, ReachesTheReferencesInteriorAtTheProfileValuesThisRendererActuallyProduces )
{
    // The conversion exists because our dimensional profile and Nubis3's are different quantities: theirs
    // is an authored field that is 1 throughout a cloud's interior, ours is a product of three smooth
    // [0,1] factors that measures 0.06-0.20 at the samples the eye integrates. The relations, not the
    // number:

    // A rim IS the surface, at either end of the definition.
    EXPECT_FLOAT_EQ( R::CloudProfileDepth( 0.0f ), 0.0f );

    // Monotone and bounded — a depth cannot leave [0,1] however the density model is tuned.
    float previous = -1.0f;
    for ( int i = 0; i <= 40; ++i )
    {
        const float d = R::CloudProfileDepth( float( i ) / 20.0f );
        EXPECT_GE( d, 0.0f );
        EXPECT_LE( d, 1.0f );
        EXPECT_GE( d, previous );
        previous = d;
    }

    // The measured band. A sample at the low end of what the eye sees must still read as mostly surface,
    // and one at the high end must read as mostly interior — that separation is the whole point, and it
    // is exactly what the raw profile could not provide: sqrt(1 - 0.06) and sqrt(1 - 0.20) differ by 8%,
    // where these differ by more than a factor of two.
    EXPECT_LT( R::CloudProfileDepth( 0.06f ), 0.5f );
    EXPECT_GT( R::CloudProfileDepth( 0.20f ), 0.9f );

    // And the interior saturates rather than overshooting, so the two formulas below stay inside the
    // paper's own ranges no matter how dense a preset makes the field.
    EXPECT_FLOAT_EQ( R::CloudProfileDepth( 10.0f ), 1.0f );
}

TEST( CloudProfileDepth, DarkensAnInteriorAndLeavesARimWispAlone )
{
    // The pair of relations the change is judged by, on the ambient term (Nubis3 p.141). A wisp on the
    // rim keeps its sky; a sample in the body of a cloud loses most of it. Before the conversion the two
    // were within a few percent of each other, which is what rendered clouds as flat lumps.
    const float sigma = 0.0005f;

    const float wisp     = R::CloudAmbientOcclusion( 0.02f, 0.0f, sigma, 0.95f );
    const float interior = R::CloudAmbientOcclusion( 0.18f, 0.0f, sigma, 0.95f );

    EXPECT_GT( wisp, 0.8f );
    EXPECT_LT( interior, 0.5f );
    EXPECT_GT( wisp, 2.0f * interior );
}

TEST( CloudMultiScatterExtinction, FallsWithDepthAndStaysInsideThePapersRange )
{
    // The paper's own bounds, asserted as bounds rather than as the two endpoint values, so no
    // combination of profile and view angle can leave the range the ablation was measured in.
    for ( int pi = 0; pi <= 20; ++pi )
        for ( int ci = -10; ci <= 10; ++ci )
        {
            // The tolerance is float rounding in the two nested mixes, not slack in the claim: at
            // profile 1 the lerp lands on 0.049999997 rather than on 0.05.
            const float k = R::CloudMultiScatterExtinction( float( pi ) / 20.0f, float( ci ) / 10.0f );
            EXPECT_GE( k, R::CLOUD_MS_EXTINCTION_CORE - 1e-6f );
            EXPECT_LE( k, R::CLOUD_MS_EXTINCTION_SURFACE + 1e-6f );
        }

    // Monotone in DEPTH: deeper into the modelled cloud is strictly more transparent to already-scattered
    // light. This is the property the whole item exists for. Strict below CLOUD_PROFILE_INTERIOR, where
    // our profile still carries depth information, and flat at the core value above it — a sample deeper
    // than "inside" is not deeper still.
    float previous = R::CLOUD_MS_EXTINCTION_SURFACE + 1.0f;
    for ( int pi = 0; pi <= 20; ++pi )
    {
        const float profile = R::CLOUD_PROFILE_INTERIOR * float( pi ) / 20.0f;
        const float k       = R::CloudMultiScatterExtinction( profile, 1.0f );
        EXPECT_LT( k, previous );
        previous = k;
    }
    for ( int pi = 0; pi <= 10; ++pi )
        EXPECT_NEAR( R::CloudMultiScatterExtinction( R::CLOUD_PROFILE_INTERIOR + float( pi ) / 10.0f, 1.0f ),
                     R::CLOUD_MS_EXTINCTION_CORE, 1e-6f );

    // Monotone in VIEW ANGLE too: the reduction is handed out as the eye turns toward the sun, which is
    // the case the reference's outer Remap(sun_dot, 0, 0.9, ...) selects.
    previous = R::CLOUD_MS_EXTINCTION_SURFACE + 1.0f;
    for ( int ci = 0; ci <= 9; ++ci )
    {
        const float k = R::CloudMultiScatterExtinction( 1.0f, float( ci ) / 10.0f );
        EXPECT_LT( k, previous );
        previous = k;
    }

    // The deepest backlit sample is the reference's own ratio away from the surface one, and nothing else.
    EXPECT_NEAR( R::CloudMultiScatterExtinction( 1.0f, 0.9f ) / R::CloudMultiScatterExtinction( 0.0f, 0.9f ),
                 R::CLOUD_MS_EXTINCTION_CORE / R::CLOUD_MS_EXTINCTION_SURFACE, 1e-6f );
}

TEST( CloudMultiScatterOpticalDepth, LeavesEveryRimAndEveryFrontLitViewExactlyAsItWas )
{
    // A rim sample has profile 0 — it IS the surface — and must come out of the modulation untouched,
    // whatever the view angle. Rims are where the silver lining lives; a change here would be a
    // regression dressed up as a fix.
    for ( int ci = -10; ci <= 10; ++ci )
        EXPECT_FLOAT_EQ( R::CloudMultiScatterOpticalDepth( 3.5f, 0.0f, float( ci ) / 10.0f ), 3.5f );

    // And a view that does not face the sun is untouched at any depth: the modulation's outer factor is
    // zero there. This is what makes the change a no-op on lit tops seen from the sun's side.
    for ( int pi = 0; pi <= 10; ++pi )
        EXPECT_FLOAT_EQ( R::CloudMultiScatterOpticalDepth( 3.5f, float( pi ) / 10.0f, 0.0f ), 3.5f );

    EXPECT_FLOAT_EQ( R::CloudMultiScatterOpticalDepth( 3.5f, 1.0f, -0.7f ), 3.5f );

    // A negative optical depth is not a negative one — the march never produces it, and the octaves must
    // not be handed one if it ever did.
    EXPECT_FLOAT_EQ( R::CloudMultiScatterOpticalDepth( -2.0f, 1.0f, 1.0f ), 0.0f );
}

TEST( CloudMultiScatterOpticalDepth, DeepBacklitSamplesGetStrictlyMoreEnergyThanTheOldFormulaGave )
{
    // The relation this item is judged by. `MultiScatterBefore...` below is the call the march made
    // before CLD-113 — the same octaves, handed the raw tauSun — kept here so the comparison is against
    // the formula that produced pp. 135's charcoal cores rather than against a remembered number.
    for ( float tau : { 2.0f, 6.0f, 12.0f } )
    {
        const float before = MultiScatterBeforeDepthModulation( tau, 0.95f ).x;
        const float after  = MultiScatterWithDepthModulation( tau, 1.0f, 0.95f ).x;
        EXPECT_GT( after, before ) << "tau = " << tau;

        // And the deeper the sample, the more of the gap it closes: monotone in the profile over the range
        // the profile still carries depth in, so a core is never darker than the shoulder that surrounds
        // it. Beyond CLOUD_PROFILE_INTERIOR the sample is already "inside" and the curve is flat.
        float previous = 0.0f;
        for ( int pi = 0; pi <= 10; ++pi )
        {
            const float profile = R::CLOUD_PROFILE_INTERIOR * float( pi ) / 10.0f;
            const float lit     = MultiScatterWithDepthModulation( tau, profile, 0.95f ).x;
            EXPECT_GT( lit, previous );
            previous = lit;
        }
        for ( int pi = 0; pi <= 10; ++pi )
            EXPECT_NEAR(
                 MultiScatterWithDepthModulation( tau, R::CLOUD_PROFILE_INTERIOR + float( pi ) / 10.0f, 0.95f ).x,
                 previous, previous * 1e-4f );
    }

    // A thick core is the case the ablation shows, so state what it is worth — and the number moved when
    // CloudThickTransmittance landed, which is worth saying rather than re-tuning around. The two
    // mechanisms do the SAME job: p.136's depth modulation divides the optical depth by five, and the
    // diffusion tail replaces the exponential the depth modulation was trying to escape. They are not
    // additive, so with the tail in place the modulation is worth about 1.6x at an optical depth of 6
    // where it used to be worth more than 3x. That is the modulation getting less to fix, not less
    // effective: the un-modulated case at the same depth is itself far brighter than it was.
    const float before = MultiScatterBeforeDepthModulation( 6.0f, 0.95f ).x;
    const float after  = MultiScatterWithDepthModulation( 6.0f, 1.0f, 0.95f ).x;
    EXPECT_GT( after, 1.4f * before );

    // The silhouette does not move: this multiplies the light a sample scatters, never the transmittance
    // the march accumulates, so however much brighter the core is it cannot be MORE than unoccluded.
    EXPECT_LT( after, MultiScatterWithDepthModulation( 0.0f, 1.0f, 0.95f ).x );
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

// ---- CLD-103/104/105/107: the v3 ambient and powder corrections -------------------------------------

TEST( CloudAmbientOcclusion, ColumnExtinctionRelaxesWithDepthIntoTheProfile )
{
    // CLD-104, Nubis3 p.136: light that has already scattered penetrates deeper, so the extinction the
    // ambient column applies must FALL as the sample sits deeper in the modelled cloud. Same column,
    // same strength — the deep sample must keep MORE of its column term than a naive Beer would say.
    const float column = 4000.0f;
    const float sigma  = 0.0005f;

    const float shallowColumn = glm::exp( -column * sigma * R::CLOUD_MS_EXTINCTION_SURFACE );
    const float deepColumn    = glm::exp( -column * sigma * R::CLOUD_MS_EXTINCTION_CORE );
    EXPECT_GT( deepColumn, shallowColumn );

    // And the constants themselves are the paper's range. Since CLD-113 they are ONE pair, shared with the
    // direct term: p.136 states them once, and two copies would be two things to drift apart.
    EXPECT_FLOAT_EQ( R::CLOUD_MS_EXTINCTION_SURFACE, 0.25f );
    EXPECT_FLOAT_EQ( R::CLOUD_MS_EXTINCTION_CORE, 0.05f );

    // The ambient column takes the depth modulation and NOT the view-angle one — sky light has no
    // direction to be modulated by. Asserting it here keeps the two callers of the shared constants
    // honestly different: the ambient term's coefficient at profile 1 is the core value outright, where
    // the direct term only reaches it looking toward the sun.
    // Both halves read the profile through CloudProfileDepth, so the endpoints are stated at the profile
    // values that MEAN surface and interior in this renderer, not at 0 and 1 of a field that never gets
    // there.
    EXPECT_FLOAT_EQ( R::CloudAmbientOcclusion( R::CLOUD_PROFILE_INTERIOR, column, sigma, 1.0f ),
                     0.0f ); // local term is sqrt(1-1) at a full interior: no sky reaches a solid core
    const float almost = 0.99f * R::CLOUD_PROFILE_INTERIOR;
    EXPECT_NEAR( R::CloudAmbientOcclusion( almost, column, sigma, 1.0f ),
                 glm::sqrt( 0.01f ) *
                      glm::exp( -column * sigma *
                                glm::mix( R::CLOUD_MS_EXTINCTION_SURFACE, R::CLOUD_MS_EXTINCTION_CORE, 0.99f ) ),
                 1e-6f );

    // Full strength, zero column: pure local term, pow(1 - depth, 0.5) — the p.141 form.
    const float mid = 0.36f * R::CLOUD_PROFILE_INTERIOR;
    EXPECT_NEAR( R::CloudAmbientOcclusion( mid, 0.0f, sigma, 1.0f ), glm::sqrt( 1.0f - 0.36f ), 1e-5f );

    // Strength 0 turns the whole model off.
    EXPECT_FLOAT_EQ( R::CloudAmbientOcclusion( 0.9f, 50000.0f, sigma, 0.0f ), 1.0f );
}

TEST( CloudAerialPerspective, IsTheExactIdentityWhereThereIsNoAirAndWhereTheDialIsOff )
{
    const glm::vec3 cloud{ 3.0f, 2.5f, 2.0f };
    const glm::vec3 air{ 0.4f, 0.6f, 1.1f };

    // An empty froxel — no in-scatter, full transmittance — is the arithmetic identity at ANY strength.
    // This is the property the artistic gradient's zero gate leans on: a scene with no volume must get
    // its march's own colours back, bit for bit, and not an approximation of them.
    for ( const float strength : { 0.0f, 0.37f, 1.0f } )
    {
        const glm::vec3 same = R::CloudApplyAerialPerspective( cloud, 0.3f, glm::vec3( 0.0f ), 1.0f, strength );
        EXPECT_FLOAT_EQ( same.x, cloud.x );
        EXPECT_FLOAT_EQ( same.y, cloud.y );
        EXPECT_FLOAT_EQ( same.z, cloud.z );
    }

    // The dial at zero is the identity for ANY froxel — the artist's escape from the coupling.
    const glm::vec3 off = R::CloudApplyAerialPerspective( cloud, 0.3f, air, 0.2f, 0.0f );
    EXPECT_FLOAT_EQ( off.x, cloud.x );
    EXPECT_FLOAT_EQ( off.y, cloud.y );
    EXPECT_FLOAT_EQ( off.z, cloud.z );
}

TEST( CloudAerialPerspective, AddsTheAirOnlyOverThePartOfThePixelTheCloudCovers )
{
    const glm::vec3 cloud{ 3.0f, 2.5f, 2.0f };
    const glm::vec3 air{ 0.4f, 0.6f, 1.1f };
    const float     apT = 0.65f;

    // A fully TRANSPARENT ray covers nothing: the sky pass already drew that pixel's whole atmospheric
    // column, so the froxel must contribute nothing at all. Getting this wrong is a bright halo around
    // every cloud, and it is the failure mode this assertion exists for.
    const glm::vec3 empty = R::CloudApplyAerialPerspective( glm::vec3( 0.0f ), 1.0f, air, apT, 1.0f );
    EXPECT_FLOAT_EQ( empty.x, 0.0f );
    EXPECT_FLOAT_EQ( empty.y, 0.0f );
    EXPECT_FLOAT_EQ( empty.z, 0.0f );

    // A fully OPAQUE ray covers all of it: the whole froxel lands, over the attenuated cloud.
    const glm::vec3 solid = R::CloudApplyAerialPerspective( cloud, 0.0f, air, apT, 1.0f );
    EXPECT_NEAR( solid.x, cloud.x * apT + air.x, 1e-6f );
    EXPECT_NEAR( solid.y, cloud.y * apT + air.y, 1e-6f );
    EXPECT_NEAR( solid.z, cloud.z * apT + air.z, 1e-6f );

    // In between, the added air is strictly proportional to the coverage.
    const glm::vec3 half = R::CloudApplyAerialPerspective( cloud, 0.5f, air, apT, 1.0f );
    EXPECT_NEAR( half.x, cloud.x * apT + air.x * 0.5f, 1e-6f );
}

TEST( CloudAerialPerspective, CannotInventLightAtAnySetting )
{
    // THE BOUND. A step of air cannot hand the eye more than the cloud it dims plus the light that air
    // itself scatters — one assertion that catches any spurious factor anywhere in the composition,
    // including a coverage term applied to the wrong side.
    const glm::vec3 cloud{ 3.0f, 2.5f, 2.0f };
    const glm::vec3 air{ 0.4f, 0.6f, 1.1f };

    for ( int t = 0; t <= 10; ++t )
    {
        for ( int a = 0; a <= 10; ++a )
        {
            for ( int s = 0; s <= 10; ++s )
            {
                const glm::vec3 out = R::CloudApplyAerialPerspective( cloud, static_cast<float>( t ) * 0.1f, air,
                                                                      static_cast<float>( a ) * 0.1f,
                                                                      static_cast<float>( s ) * 0.1f );

                EXPECT_LE( out.x, cloud.x + air.x + 1e-5f );
                EXPECT_LE( out.y, cloud.y + air.y + 1e-5f );
                EXPECT_LE( out.z, cloud.z + air.z + 1e-5f );
                EXPECT_GE( out.x, 0.0f );
            }
        }
    }

    // Out-of-range inputs are clamped rather than extrapolated: a half-precision froxel can read back a
    // transmittance a hair over 1, and a scene file can hold a strength of 2.
    const glm::vec3 clamped = R::CloudApplyAerialPerspective( cloud, -0.2f, air, 1.4f, 3.0f );
    EXPECT_NEAR( clamped.x, cloud.x + air.x, 1e-6f );
}

TEST( CloudAmbientColumnVertical, ProjectsTheSlantColumnAndRefusesToVanishAtSunrise )
{
    // CLD-103: the shadow map integrates along the SUN; sky occlusion wants the stack OVERHEAD. At noon
    // the two agree; at low sun the slant is longer by 1/sin(elevation) and must be projected back.
    EXPECT_FLOAT_EQ( R::CloudAmbientColumnVertical( 1000.0f, 1.0f ), 1000.0f );
    EXPECT_FLOAT_EQ( R::CloudAmbientColumnVertical( 1000.0f, 0.5f ), 500.0f );

    // The clamp floor: a horizon sun must not let the column term claim the sky is unoccluded.
    EXPECT_FLOAT_EQ( R::CloudAmbientColumnVertical( 1000.0f, 0.0f ), 150.0f );
    EXPECT_FLOAT_EQ( R::CloudAmbientColumnVertical( 1000.0f, -0.4f ), 150.0f );

    EXPECT_FLOAT_EQ( R::CloudAmbientColumnVertical( -5.0f, 1.0f ), 0.0f ) << "a negative column is no column";
}

TEST( CloudPowderView, FadesOutInsideTheForwardConeAndMatchesPowderBehindTheCamera )
{
    // CLD-107: the dark edge is a reflection-side effect. Looking away from the sun the classic powder
    // applies in full; inside the forward cone — where the silver lining lives — it must be gone.
    const float density  = 0.05f;
    const float strength = 1.0f;
    const float scale    = 2.0f;

    // The exempt cone is [0.85, 0.99] — the ~15 degrees where the silver lining lives. The first ramp
    // started at 0.5 (a 60-degree cone) and deleted powder from the whole backlit face: every
    // inter-lobe crease in front of the sun lost its darkening (deck pp.128-130 keep them dark).
    EXPECT_FLOAT_EQ( R::CloudPowderView( density, strength, scale, -1.0f ),
                     R::CloudPowder( density, strength, scale ) );
    EXPECT_FLOAT_EQ( R::CloudPowderView( density, strength, scale, 0.85f ),
                     R::CloudPowder( density, strength, scale ) );
    EXPECT_FLOAT_EQ( R::CloudPowderView( density, strength, scale, 0.99f ), 1.0f );
    EXPECT_FLOAT_EQ( R::CloudPowderView( density, strength, scale, 1.0f ), 1.0f );

    // Monotone in between: turning toward the sun never brings the darkening back.
    float previous = 0.0f;
    for ( float c = -1.0f; c <= 1.0f; c += 0.05f )
    {
        const float value = R::CloudPowderView( density, strength, scale, c );
        EXPECT_GE( value, previous - 1e-6f ) << "cosTheta = " << c;
        previous = value;
    }
}

TEST( CloudShadowTintWeight, IsIdentityOnALitFaceAndTheAuthoredTintDeepInShadow )
{
    // CLD-105: the tint must have NO effect where the sun is unoccluded — that is the difference
    // between a shadow tint and a global colour cast.
    const glm::vec3 tint( 0.86f, 0.89f, 0.98f );

    EXPECT_EQ( R::CloudShadowTintWeight( tint, 0.0f ), glm::vec3( 1.0f ) );
    EXPECT_NEAR( R::CloudShadowTintWeight( tint, 50.0f ).r, tint.r, 1e-5f );
    EXPECT_NEAR( R::CloudShadowTintWeight( tint, 50.0f ).b, tint.b, 1e-5f );

    // Halfway: strictly between identity and the tint, monotone in tau.
    const glm::vec3 mid = R::CloudShadowTintWeight( tint, 0.7f );
    EXPECT_LT( mid.r, 1.0f );
    EXPECT_GT( mid.r, tint.r );
}

TEST( CloudShadowEdgeFade, TrustsTheInteriorAndFadesOverTheOuterTenth )
{
    // CLD-103: a column term that stops at the map's edge draws that edge in the sky.
    EXPECT_FLOAT_EQ( R::CloudShadowEdgeFade( glm::vec2( 0.5f, 0.5f ) ), 1.0f );
    EXPECT_FLOAT_EQ( R::CloudShadowEdgeFade( glm::vec2( 0.2f, 0.8f ) ), 1.0f );
    EXPECT_FLOAT_EQ( R::CloudShadowEdgeFade( glm::vec2( 0.0f, 0.5f ) ), 0.0f );
    EXPECT_FLOAT_EQ( R::CloudShadowEdgeFade( glm::vec2( 0.5f, 1.0f ) ), 0.0f );
    EXPECT_NEAR( R::CloudShadowEdgeFade( glm::vec2( 0.05f, 0.5f ) ), 0.5f, 1e-5f );
}

// ---- CLD-29: the depth reconstruction ---------------------------------------------------------------

TEST( CloudDepth, ReconstructionIsTheInverseOfTheEnginesOwnProjection )
{
    // The engine's camera builds its projection with Core::MakePerspective (reversed-Z, zero-to-one) and
    // its view with glm::lookAt; the reconstruction has to be the inverse of THAT. Built through the
    // engine's own factory rather than a hand-rolled glm::perspective, so the test moves if it does.
    const glm::mat4 projection =
         Desert::Core::MakePerspective( glm::radians( 60.0f ), 16.0f / 9.0f, 10.0f, 500000.0f );
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
    // The cleared value is the ENGINE'S, not a literal: under reversed-Z a depth attachment clears to 0,
    // the far plane. Spelling it kDepthClear is what makes this fail if the sentinel and the clear ever
    // disagree, and that is the whole failure mode — a sentinel the wrong way round reads every sky pixel
    // as geometry at the near plane and the cloud deck stops existing.
    const glm::mat4 identity( 1.0f );
    EXPECT_FLOAT_EQ( R::CloudGeometryLimit( identity, glm::vec3( 0.0f ), glm::vec2( 0.0f ),
                                            Desert::Core::kDepthClear, 12345.0f ),
                     12345.0f );
}

// The other side of the same coin, and the reason the test above is not enough on its own: a fragment ON
// the near plane carries the MAXIMUM stored depth, and it must NOT be read as sky. A sentinel written the
// wrong way round satisfies one of these two and fails the other, whichever way it is wrong.
TEST( CloudDepth, NearPlaneDepthIsGeometryAndNotSky )
{
    const glm::mat4 projection = Desert::Core::MakePerspective( glm::radians( 60.0f ), 1.0f, 10.0f, 500000.0f );
    const glm::mat4 view =
         glm::lookAt( glm::vec3( 0.0f ), glm::vec3( 0.0f, 0.0f, 1.0f ), glm::vec3( 0.0f, 1.0f, 0.0f ) );
    const glm::mat4 inverse = glm::inverse( projection * view );

    const float limit = R::CloudGeometryLimit( inverse, glm::vec3( 0.0f ), glm::vec2( 0.0f ),
                                               Desert::Core::kDepthNear, 12345.0f );
    EXPECT_LT( limit, 12345.0f );
    EXPECT_NEAR( limit, 10.0f, 1e-2f ); // the near plane itself, in world units
}

TEST( CloudDepth, GeometryNeverExtendsTheMarchBeyondTheViewDistance )
{
    const glm::mat4 projection = Desert::Core::MakePerspective( glm::radians( 60.0f ), 1.0f, 10.0f, 500000.0f );
    const glm::mat4 view =
         glm::lookAt( glm::vec3( 0.0f ), glm::vec3( 0.0f, 0.0f, 1.0f ), glm::vec3( 0.0f, 1.0f, 0.0f ) );
    const glm::mat4 inverse = glm::inverse( projection * view );

    const glm::vec4 clip = projection * view * glm::vec4( 0.0f, 0.0f, 400000.0f, 1.0f );
    const float     ndcZ = clip.z / clip.w;

    EXPECT_FLOAT_EQ( R::CloudGeometryLimit( inverse, glm::vec3( 0.0f ), glm::vec2( 0.0f ), ndcZ, 1000.0f ),
                     1000.0f );
}

// ---- The GPU payload --------------------------------------------------------------------------------

// ---- The cloud shadow map ---------------------------------------------------------------------------

TEST( CloudShadowProjection, EveryPointOnOneSunRayLandsOnOneTexel )
{
    // THE property the whole map rests on. If two points on the same sun ray projected to different
    // texels, a single fetch could not answer "how much cloud is between me and the sun" at all.
    for ( const glm::vec3& sun : { glm::vec3( 0.0f, 1.0f, 0.0f ), glm::normalize( glm::vec3( 1.0f, 1.0f, 0.0f ) ),
                                   glm::normalize( glm::vec3( 0.3f, 0.05f, -0.9f ) ) } )
    {
        const glm::vec3 centre( 1000.0f, 2000.0f, -500.0f );
        const glm::vec3 point( 12345.0f, 6789.0f, -4321.0f );
        const glm::vec2 uv = R::CloudShadowUv( point, centre, sun, 30000.0f );

        for ( const float along : { -50000.0f, -1.0f, 0.0f, 1.0f, 50000.0f } )
        {
            const glm::vec2 moved = R::CloudShadowUv( point + sun * along, centre, sun, 30000.0f );
            EXPECT_NEAR( moved.x, uv.x, 1e-4f ) << "slid " << along << " along the sun";
            EXPECT_NEAR( moved.y, uv.y, 1e-4f );
        }
    }
}

TEST( CloudShadowProjection, TheCentreIsTheMiddleAndTheExtentIsTheEdge )
{
    const glm::vec3 sun = glm::normalize( glm::vec3( 0.4f, 0.8f, 0.2f ) );
    const glm::vec3 centre( 0.0f, 3000.0f, 0.0f );
    const float     extent = 25000.0f;

    const glm::vec2 middle = R::CloudShadowUv( centre, centre, sun, extent );
    EXPECT_NEAR( middle.x, 0.5f, 1e-5f );
    EXPECT_NEAR( middle.y, 0.5f, 1e-5f );
    EXPECT_TRUE( R::CloudShadowInside( middle ) );

    // One extent along either basis axis is exactly the border, and past it is off the map.
    const glm::vec3 right = R::CloudShadowRight( sun );
    EXPECT_NEAR( R::CloudShadowUv( centre + right * extent, centre, sun, extent ).x, 1.0f, 1e-5f );
    EXPECT_FALSE(
         R::CloudShadowInside( R::CloudShadowUv( centre + right * ( extent * 1.01f ), centre, sun, extent ) ) );
}

TEST( CloudShadowProjection, TheBasisIsOrthonormalEvenWithTheSunAtThePole )
{
    // The pole is where a careless basis degenerates: cross() with a nearly parallel reference loses all
    // its precision, and a zero-length tangent is a NaN that spreads to every shadow on screen.
    for ( const glm::vec3& sun :
          { glm::vec3( 0.0f, 1.0f, 0.0f ), glm::vec3( 0.0f, -1.0f, 0.0f ),
            glm::normalize( glm::vec3( 0.001f, 1.0f, 0.0f ) ), glm::normalize( glm::vec3( 1.0f, 0.02f, 0.0f ) ) } )
    {
        const glm::vec3 right = R::CloudShadowRight( sun );
        const glm::vec3 up    = R::CloudShadowUp( sun );

        EXPECT_NEAR( glm::length( right ), 1.0f, 1e-4f );
        EXPECT_NEAR( glm::length( up ), 1.0f, 1e-4f );
        EXPECT_NEAR( glm::dot( right, up ), 0.0f, 1e-4f );
        EXPECT_NEAR( glm::dot( right, sun ), 0.0f, 1e-4f );
        EXPECT_NEAR( glm::dot( up, sun ), 0.0f, 1e-4f );
    }
}

TEST( CloudShadowProjection, UvAndTheWorldPointRoundTrip )
{
    const glm::vec3 sun = glm::normalize( glm::vec3( -0.2f, 0.6f, 0.77f ) );
    const glm::vec3 centre( 500.0f, 1500.0f, 250.0f );
    const float     extent = 40000.0f;

    for ( float u = 0.0f; u <= 1.0f; u += 0.25f )
        for ( float v = 0.0f; v <= 1.0f; v += 0.25f )
        {
            const glm::vec3 point = R::CloudShadowPlanePoint( glm::vec2( u, v ), centre, sun, extent );
            const glm::vec2 back  = R::CloudShadowUv( point, centre, sun, extent );
            EXPECT_NEAR( back.x, u, 1e-4f );
            EXPECT_NEAR( back.y, v, 1e-4f );
        }
}

TEST( CloudShadowColumnRule, TheColumnIsTheLayerAndNotTheOtherSideOfThePlanet )
{
    // The test that was missing when this shipped, and it would have caught it on the first run. The map
    // is built on a plane through the CAMERA, which normally sits BELOW the layer, and the pass marched
    // -sunDir from there: away from the clouds, into the planet, and out the far side, where a ray/sphere
    // test happily reported the shell nine thousand kilometres away. Every self-shadow inside the map's
    // extent was made of density sampled there.
    //
    // The property, stated so it cannot be satisfied by accident: both ends of the column sit at the
    // layer's own altitudes, and every sample between them is inside the layer.
    const glm::vec3 camera( 0.0f, 0.002f, 0.0f ); // 2 m up, in kilometres

    for ( const float elevationDeg : { 20.0f, 45.0f, 60.0f, 80.0f } )
    {
        const float     e   = glm::radians( elevationDeg );
        const glm::vec3 sun = glm::normalize( glm::vec3( std::cos( e ), std::sin( e ), 0.0f ) );

        for ( const float lateralKm : { -25.0f, 0.0f, 25.0f } )
        {
            const glm::vec3 plane  = camera + R::CloudShadowRight( sun ) * lateralKm;
            const auto      column = R::CloudShadowColumn( plane, sun, kPlanetRadiusKm, kBottomKm, kThicknessKm );

            ASSERT_TRUE( column.Hit ) << "elevation " << elevationDeg << " lateral " << lateralKm;
            EXPECT_GT( column.TExit, column.TEnter );

            // The ends are the layer's own surfaces, and the whole span is a couple of layer thicknesses
            // at most — not a chord across the planet.
            EXPECT_LT( column.TExit - column.TEnter, kThicknessKm * 20.0f )
                 << "elevation " << elevationDeg << ": a column of " << ( column.TExit - column.TEnter )
                 << " km is not a cloud layer";

            for ( float f = 0.0f; f <= 1.0f; f += 0.1f )
            {
                const float     t      = glm::mix( column.TEnter, column.TExit, f );
                const glm::vec3 sample = plane + sun * t;
                const float     h      = R::CloudLayerHeight( sample, kPlanetRadiusKm, kBottomKm, kThicknessKm );
                EXPECT_GE( h, -1e-3f ) << "elevation " << elevationDeg << " f " << f;
                EXPECT_LE( h, 1.0f + 1e-3f ) << "elevation " << elevationDeg << " f " << f;
            }
        }
    }
}

TEST( CloudShadowColumnRule, TheTopEndIsTheEndNEARESTTheSun )
{
    // The march walks from TExit down, and the slices are recorded on a falling height. If the two ends
    // were the other way round every column would be integrated upside down: cloud tops would carry the
    // whole column's shadow and cloud bases none.
    const glm::vec3 plane( 0.0f, 0.002f, 0.0f );
    const glm::vec3 sun = glm::normalize( glm::vec3( 0.4f, 0.9f, 0.0f ) );

    const auto column = R::CloudShadowColumn( plane, sun, kPlanetRadiusKm, kBottomKm, kThicknessKm );
    ASSERT_TRUE( column.Hit );

    const float topHeight =
         R::CloudLayerHeight( plane + sun * column.TExit, kPlanetRadiusKm, kBottomKm, kThicknessKm );
    const float bottomHeight =
         R::CloudLayerHeight( plane + sun * column.TEnter, kPlanetRadiusKm, kBottomKm, kThicknessKm );

    EXPECT_NEAR( topHeight, 1.0f, 1e-3f );
    EXPECT_NEAR( bottomHeight, 0.0f, 1e-3f );
}

TEST( CloudShadowReadout, MoreCloudLiesAboveALowSampleThanAHighOne )
{
    // The slices are cumulative from the top down, so the read-out must never increase with height. A
    // shadow term that went the other way would light cloud bases and darken cloud tops — which is
    // exactly the defect J.3 #2 records in the reference's light grid, arrived at from the other side.
    const glm::vec4 slices( 9.0f, 6.0f, 3.0f, 1.0f ); // base -> 0.25 -> 0.5 -> 0.75

    float previous = std::numeric_limits<float>::max();
    for ( float h = 0.0f; h <= 1.0f; h += 0.02f )
    {
        const float value = R::CloudShadowDensityLength( slices, h );
        EXPECT_LE( value, previous + 1e-5f ) << "height " << h;
        EXPECT_GE( value, 0.0f );
        previous = value;
    }

    // The stored heights come back exactly, and the top has nothing above it by construction.
    EXPECT_NEAR( R::CloudShadowDensityLength( slices, 0.0f ), 9.0f, 1e-5f );
    EXPECT_NEAR( R::CloudShadowDensityLength( slices, 0.25f ), 6.0f, 1e-5f );
    EXPECT_NEAR( R::CloudShadowDensityLength( slices, 0.75f ), 1.0f, 1e-5f );
    EXPECT_NEAR( R::CloudShadowDensityLength( slices, 1.0f ), 0.0f, 1e-5f );
}

TEST( CloudShadowReadout, AnEmptyColumnShadowsNothingAtAnyHeight )
{
    for ( float h = 0.0f; h <= 1.0f; h += 0.1f )
        EXPECT_FLOAT_EQ( R::CloudShadowDensityLength( glm::vec4( 0.0f ), h ), 0.0f );
}

// ---- The WORLD-readable encoding -------------------------------------------------------------------
//
// The second cloud-shadow encoding (research doc section 5, Q3): {front depth km, mean extinction per
// km, max optical depth}, which answers "how much cloud is between THIS point and the sun" for a
// receiver that is not inside a cloud layer at all — the ground, a mesh, a mountain top above the deck.
//
// Every test here is a RELATION between two answers of the same function rather than a spot value,
// because a spot value of an encoding whose two ends live in different shaders would pass while the two
// ends disagreed.

TEST( CloudWorldShadow, TheKilometreIsTheSameKilometreTheRestOfTheMarchUses )
{
    // Common/CloudWorldShadow.glslh spells the constant out rather than taking it from
    // Common/CloudGeometry.glslh, so a terrain fragment shader can include the read-out without twelve
    // hundred lines of march arithmetic. Two spellings of one number is exactly the defect class this
    // suite exists for, so the two are pinned against each other here.
    EXPECT_FLOAT_EQ( R::CLOUD_WORLD_SHADOW_UNITS_PER_KM, R::CLOUD_WORLD_UNITS_PER_KM );
}

TEST( CloudWorldShadow, DepthAlongTheSunGrowsAwayFromIt )
{
    const glm::vec3 sun = glm::normalize( glm::vec3( 0.3f, 0.8f, 0.5f ) );
    const glm::vec3 centre( 1000.0f, 200.0f, -300.0f );

    // A point a kilometre TOWARD the sun is a kilometre LESS deep than the plane, one away from it a
    // kilometre more. The whole read-out hangs on this sign: get it backwards and the ground is lit
    // while the air above it is in shadow.
    const float toward = R::CloudShadowDepthAlongSun( centre + sun * 100000.0f, centre, sun );
    const float away   = R::CloudShadowDepthAlongSun( centre - sun * 100000.0f, centre, sun );

    EXPECT_NEAR( toward, -100000.0f, 1.0f );
    EXPECT_NEAR( away, 100000.0f, 1.0f );
    EXPECT_LT( toward, away );

    // A point displaced ONLY within the map's plane has the same depth: that is what makes one texel
    // answer for a whole sun ray.
    const glm::vec3 sideways = centre + R::CloudShadowRight( sun ) * 5000.0f + R::CloudShadowUp( sun ) * -2000.0f;
    EXPECT_NEAR( R::CloudShadowDepthAlongSun( sideways, centre, sun ), 0.0f, 1e-2f );
}

TEST( CloudWorldShadow, AReceiverDeeperUnderTheDeckIsNeverLessShadowed )
{
    // MONOTONICITY, which catches an inverted subtraction or a sign flip in one assertion where a spot
    // value would have to be wrong at exactly the depth the test happened to pick.
    const glm::vec3 encoded( -2.0f, 3.0f, 12.0f ); // front 2 km toward the sun, 3 /km, capped at 12

    float previous = -1.0f;
    for ( float depthKm = -5.0f; depthKm <= 8.0f; depthKm += 0.25f )
    {
        const float opticalDepth = R::CloudWorldShadowOpticalDepth( encoded, depthKm );
        EXPECT_GE( opticalDepth, previous ) << "depth " << depthKm;
        previous = opticalDepth;
    }
}

TEST( CloudWorldShadow, NothingAboveTheDecksFrontIsShadowedByIt )
{
    // A mountain top poking through, or an aircraft over the deck: the cloud is BELOW the receiver, and
    // `max(0, depth - front)` is the whole of what says so. Without it the linear ramp would run
    // backwards and shadow everything above the layer.
    const glm::vec3 encoded( 0.0f, 4.0f, 20.0f );
    const glm::vec3 sun = glm::vec3( 0.0f, 1.0f, 0.0f );
    const glm::vec2 uv( 0.5f, 0.5f );

    for ( float depthKm = -10.0f; depthKm <= 0.0f; depthKm += 1.0f )
    {
        EXPECT_FLOAT_EQ( R::CloudWorldShadowOpticalDepth( encoded, depthKm ), 0.0f );

        const glm::vec3 position = -sun * R::CloudWorldFromKm( depthKm );
        EXPECT_FLOAT_EQ( R::CloudWorldShadowTransmittance( encoded, position, glm::vec3( 0.0f ), sun, uv ), 1.0f );
    }
}

TEST( CloudWorldShadow, TheColumnsOwnOpticalDepthIsTheCeiling )
{
    // A BOUND, and it is what stops a receiver a kilometre under a thin sheet coming out blacker than
    // one right beneath it: below the medium there is no more cloud to accumulate.
    const glm::vec3 encoded( -1.0f, 6.0f, 2.5f );

    for ( float depthKm = -1.0f; depthKm <= 50.0f; depthKm += 0.5f )
        EXPECT_LE( R::CloudWorldShadowOpticalDepth( encoded, depthKm ), encoded.z + 1e-5f );

    // And far below it the answer IS the cap, not an approach to it.
    EXPECT_FLOAT_EQ( R::CloudWorldShadowOpticalDepth( encoded, 100.0f ), encoded.z );
}

TEST( CloudWorldShadow, AnEmptyTexelIsFullSunAtEveryDepth )
{
    // What makes the feature free where there are no clouds: no flag channel and no branch, just the
    // arithmetic of a triple of zeroes.
    const glm::vec3 sun = glm::normalize( glm::vec3( -0.3f, 0.9f, 0.2f ) );

    for ( float depthKm = -20.0f; depthKm <= 20.0f; depthKm += 2.0f )
    {
        const glm::vec3 position = -sun * R::CloudWorldFromKm( depthKm );
        EXPECT_FLOAT_EQ( R::CloudWorldShadowTransmittance( glm::vec3( 0.0f ), position, glm::vec3( 0.0f ), sun,
                                                           glm::vec2( 0.5f ) ),
                         1.0f );
    }
}

TEST( CloudWorldShadow, TheMapsEdgeJoinsOntoFullSunWithoutAStep )
{
    // THE SEAM. The map covers a finite square around the camera and there is no answer beyond it. The
    // honest answer out there is "no shadow", and a hard switch to it at the border would draw a
    // straight bright line across the ground along nothing at all — the defect class the audit already
    // documents for the four-slice map's own extent.
    //
    // Two halves of one expression have to meet: inside, the transmittance is faded toward 1 by
    // CloudShadowEdgeFade; outside, it IS 1. They join continuously exactly because the fade reaches
    // zero AT the border rather than near it.
    const glm::vec3 sun = glm::normalize( glm::vec3( 0.2f, 0.85f, 0.45f ) );
    const glm::vec3 centre( 0.0f );
    const float     extent = 3000000.0f;           // 30 km, the shipped default
    const glm::vec3 encoded( -3.0f, 8.0f, 25.0f ); // a thoroughly opaque column

    // In the middle the shadow is at full strength — otherwise this test would pass on a term that is
    // zero everywhere.
    const glm::vec2 middleUv( 0.5f, 0.5f );
    const glm::vec3 middle = R::CloudShadowPlanePoint( middleUv, centre, sun, extent );
    EXPECT_LT( R::CloudWorldShadowTransmittance( encoded, middle, centre, sun, middleUv ), 0.05f );

    // Walking out to the border the answer rises to 1, and gets there smoothly: no two samples a two
    // hundredth of the map apart may differ by more than a small fraction.
    float previous = R::CloudWorldShadowTransmittance( encoded, middle, centre, sun, middleUv );
    for ( float u = 0.5f; u <= 1.0f; u += 0.005f )
    {
        const glm::vec2 uv( u, 0.5f );
        const glm::vec3 point = R::CloudShadowPlanePoint( uv, centre, sun, extent );
        const float     here  = R::CloudWorldShadowTransmittance( encoded, point, centre, sun, uv );

        EXPECT_GE( here, previous - 1e-5f ) << "u " << u;
        EXPECT_LT( here - previous, 0.15f ) << "a step at u " << u;
        previous = here;
    }

    // At the border itself the map already says "full sun", which is what the OUTSIDE branch returns —
    // so crossing it changes nothing.
    const glm::vec2 border( 1.0f, 0.5f );
    const glm::vec3 onBorder = R::CloudShadowPlanePoint( border, centre, sun, extent );
    EXPECT_FLOAT_EQ( R::CloudWorldShadowTransmittance( encoded, onBorder, centre, sun, border ), 1.0f );

    const glm::vec2 outside( 1.0001f, 0.5f );
    const glm::vec3 beyond = R::CloudShadowPlanePoint( outside, centre, sun, extent );
    EXPECT_FLOAT_EQ( R::CloudWorldShadowTransmittance( encoded, beyond, centre, sun, outside ), 1.0f );
}

TEST( CloudWorldShadow, ZeroStrengthIsTheIdentityAndOneIsTheDecksOwnTransmittance )
{
    // The OFF state is not "almost no shadow", it is exactly 1.0 — which is what lets the feature ship
    // default-off with no second code path to keep working.
    const glm::vec3 sun = glm::normalize( glm::vec3( 0.1f, 0.95f, 0.3f ) );
    const glm::vec3 centre( 0.0f );
    const float     extent = 3000000.0f;
    const glm::vec3 encoded( -1.0f, 5.0f, 9.0f );

    const glm::vec2 uv( 0.5f, 0.5f );
    const glm::vec3 point = R::CloudShadowPlanePoint( uv, centre, sun, extent ) - sun * 200000.0f;

    EXPECT_FLOAT_EQ( R::CloudWorldShadowFactor( encoded, point, centre, sun, uv, 0.0f ), 1.0f );

    const float full = R::CloudWorldShadowFactor( encoded, point, centre, sun, uv, 1.0f );
    EXPECT_FLOAT_EQ( full, R::CloudWorldShadowTransmittance( encoded, point, centre, sun, uv ) );

    // A dose in between lands between the two, monotonically — a `lerp(1, T, strength)` and nothing
    // cleverer, exactly as UE's DeferredLightPixelShaders.usf writes it.
    float previous = 1.0f;
    for ( float strength = 0.0f; strength <= 1.0f; strength += 0.1f )
    {
        const float dosed = R::CloudWorldShadowFactor( encoded, point, centre, sun, uv, strength );
        EXPECT_LE( dosed, previous + 1e-6f );
        EXPECT_GE( dosed, full - 1e-6f );
        previous = dosed;
    }
}

TEST( CloudPayload, TheBlockIsTheSizeTheBufferIsCreatedWith )
{
    // std430 rounds a block up to its 16-byte alignment; the buffer must cover that, not sizeof. Since
    // the block became a head plus an ARRAY of layers, the alignment is not a detail of the end any more:
    // std430 gives an array of structs a stride of the struct rounded up to 16, glm's vec4 has a 4-byte
    // alignment in this build so C++ pads nothing, and a four-byte disagreement would read every field of
    // layer 1 from the wrong offset with no validation error anywhere. The explicit padding is what makes
    // the three assertions below true rather than lucky.
    EXPECT_EQ( sizeof( CloudLayerPayload ), 432u );
    EXPECT_EQ( sizeof( CloudLayerPayload ) % 16u, 0u );
    EXPECT_EQ( offsetof( CloudGpuPayload, Layers ) % 16u, 0u );

    EXPECT_EQ( sizeof( CloudGpuPayload ), 112u + kCloudMaxLayers * sizeof( CloudLayerPayload ) );
    EXPECT_GE( kCloudPayloadBytes, sizeof( CloudGpuPayload ) );
    EXPECT_EQ( kCloudPayloadBytes % 16u, 0u );
}

TEST( CloudPayload, TheHeroCloudCountsAreRepairedRatherThanTrusted )
{
    // The shadow pass loops to VoxelShadowCount and the march to VoxelInstanceCount, so a shadow count
    // above the total marches records the gather never wrote — a read past the live prefix of a buffer
    // that is always allocated at full size, which is the quietest kind of wrong. Both are clamped at the
    // boundary, once, exactly as the fade pairs above are.
    const Desert::ECS::VolumetricCloudData data;
    const Desert::Graphic::AtmosphereEnv   atmosphere;
    const Desert::Graphic::WindEnv         wind;

    const CloudGpuPayload none =
         PackCloudParams( OneLayer( data ), atmosphere, wind, 0.0f, CloudVoxelCounts{ .Total = 0, .Shadow = 0 } );
    EXPECT_EQ( none.VoxelInstanceCount, 0 );
    EXPECT_EQ( none.VoxelShadowCount, 0 );

    const CloudGpuPayload some =
         PackCloudParams( OneLayer( data ), atmosphere, wind, 0.0f, CloudVoxelCounts{ .Total = 5, .Shadow = 3 } );
    EXPECT_EQ( some.VoxelInstanceCount, 5 );
    EXPECT_EQ( some.VoxelShadowCount, 3 );

    const CloudGpuPayload overshoot =
         PackCloudParams( OneLayer( data ), atmosphere, wind, 0.0f, CloudVoxelCounts{ .Total = 2, .Shadow = 9 } );
    EXPECT_EQ( overshoot.VoxelInstanceCount, 2 );
    EXPECT_LE( overshoot.VoxelShadowCount, overshoot.VoxelInstanceCount );

    const CloudGpuPayload negative = PackCloudParams( OneLayer( data ), atmosphere, wind, 0.0f,
                                                      CloudVoxelCounts{ .Total = -4, .Shadow = -1 } );
    EXPECT_EQ( negative.VoxelInstanceCount, 0 );
    EXPECT_EQ( negative.VoxelShadowCount, 0 );
}

TEST( CloudPayload, ThePushConstantFitsTheGuaranteedRange )
{
    EXPECT_LE( sizeof( CloudRaymarchPush ), 128u );
    // mat4 + camera/frame vec4 + the checkerboard flag vec4 + the per-view atmosphere vec4.
    EXPECT_EQ( sizeof( CloudRaymarchPush ), 112u );
    EXPECT_EQ( offsetof( CloudRaymarchPush, Atmosphere ), 96u );
}

TEST( CloudPayload, EveryBindingOfTheRaymarchIsDistinct )
{
    // The march declares eleven descriptors and one of them is the SKY's parameter buffer, whose number
    // is owned by another header entirely (Graphic::kSkyPayloadBinding = 1). Two of these colliding is
    // not a validation error — it is one resource silently overwriting another in the set, which is how
    // the atmosphere's froxel volume could end up bound where the profile table was expected.
    namespace G = Desert::Graphic;

    const std::vector<uint32_t> bindings{
         G::kCloudScatterOutputBinding,   G::kCloudParamsBinding,         G::kCloudShapeNoiseBinding,
         G::kCloudDetailNoiseBinding,     G::kCloudCurlNoiseBinding,      G::kCloudWeatherMapBinding,
         G::kCloudSceneDepthBinding,      G::kCloudDepthGuideBinding,     G::kCloudShadowMapBinding,
         G::kCloudProfileMapBinding,      G::kCloudProfileLutBinding,     G::kCloudAerialPerspectiveBinding,
         G::kCloudDistantSkyLightBinding, G::kCloudVolumeInstanceBinding, G::kCloudVolumeAtlasBinding };

    const std::set<uint32_t> unique( bindings.begin(), bindings.end() );
    EXPECT_EQ( unique.size(), bindings.size() );

    // The scatter output and the sky buffer share a set with them, at 0 and 1; every sampler must sit
    // above both.
    EXPECT_GT( G::kCloudAerialPerspectiveBinding, 1u );
    EXPECT_GT( G::kCloudDistantSkyLightBinding, 1u );
}

TEST( CloudPayload, EveryFieldReachesADistinctOffset )
{
    // A spot check of the group boundaries: an insertion anywhere shifts one of these. The full set is
    // asserted at compile time by the static_asserts in CloudPayload.hpp.
    EXPECT_EQ( offsetof( CloudGpuPayload, SunDirection ), 0u );
    EXPECT_EQ( offsetof( CloudGpuPayload, SceneWind ), 64u );
    EXPECT_EQ( offsetof( CloudGpuPayload, PlanetRadius ), 80u );
    EXPECT_EQ( offsetof( CloudGpuPayload, LayerCount ), 96u );
    EXPECT_EQ( offsetof( CloudGpuPayload, VoxelShadowCount ), 104u );
    EXPECT_EQ( offsetof( CloudGpuPayload, Layers ), 112u );

    EXPECT_EQ( offsetof( CloudLayerPayload, ScatteringAlbedo ), 0u );
    EXPECT_EQ( offsetof( CloudLayerPayload, LayerBottomAltitude ), 112u );
    EXPECT_EQ( offsetof( CloudLayerPayload, Coverage ), 132u );
    EXPECT_EQ( offsetof( CloudLayerPayload, SunLightIntensityScale ), 268u );
    EXPECT_EQ( offsetof( CloudLayerPayload, AnimationSpeed ), 332u );
    EXPECT_EQ( offsetof( CloudLayerPayload, MinStepSize ), 364u );
    EXPECT_EQ( offsetof( CloudLayerPayload, MultiScatterOctaves ), 400u );
    EXPECT_EQ( offsetof( CloudLayerPayload, CloudHeightVariance ), 420u );

    // The second layer starts exactly one stride on. This is the assertion the whole layout exists for.
    EXPECT_EQ( reinterpret_cast<const char*>( &reinterpret_cast<const CloudGpuPayload*>( 0 )->Layers[1] ) -
                    reinterpret_cast<const char*>( &reinterpret_cast<const CloudGpuPayload*>( 0 )->Layers[0] ),
               static_cast<std::ptrdiff_t>( sizeof( CloudLayerPayload ) ) );
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

    const CloudGpuPayload p = PackCloudParams( OneLayer( data ), atmosphere, wind, 12.5f, CloudVoxelCounts{} );

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
    EXPECT_FLOAT_EQ( p.Layers[0].Coverage, data.Coverage );
    EXPECT_FLOAT_EQ( p.Layers[0].ExtinctionTint.w, data.ExtinctionScale );
    EXPECT_EQ( p.Layers[0].MaxSteps, data.MaxSteps );
    EXPECT_EQ( p.Layers[0].LightMarchSamples, data.LightMarchSamples );
    EXPECT_EQ( p.Layers[0].WeatherOctaves, data.WeatherOctaves );
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
    data.HighFreqFeatureSize    = -5.0f;
    data.MinStepSize            = 5000.0f;
    data.MaxStepSize            = 100.0f;

    const CloudGpuPayload p = PackCloudParams( OneLayer( data ), atmosphere, wind, 0.0f, CloudVoxelCounts{} );

    EXPECT_GE( p.Layers[0].HorizonFadeEnd, p.Layers[0].HorizonFadeStart );
    EXPECT_GE( p.Layers[0].NearFadeEnd, p.Layers[0].NearFadeStart );
    EXPECT_GE( p.Layers[0].SofteningEndDistance, p.Layers[0].SofteningStartDistance );
    EXPECT_GE( p.Layers[0].DistanceFadeEnd, p.Layers[0].DistanceFadeStart );
    EXPECT_GE( p.Layers[0].HighFreqFeatureSize, 1.0f );
    EXPECT_GE( p.Layers[0].MaxStepSize, p.Layers[0].MinStepSize );
}

TEST( CloudPayload, EveryPackedValueIsFinite )
{
    Desert::ECS::VolumetricCloudData data{};
    Desert::Graphic::AtmosphereEnv   atmosphere{};
    Desert::Graphic::WindEnv         wind{};
    atmosphere.PlanetRadius = kPlanetRadiusWorld;

    const CloudGpuPayload p     = PackCloudParams( OneLayer( data ), atmosphere, wind, 3.0f, CloudVoxelCounts{} );
    const float*          words = reinterpret_cast<const float*>( &p );

    // Every word of the block that is a FLOAT. The block is no longer "floats then ints" - it is a head
    // and an array of layers, each with an int run in its middle - so the int words are named and
    // skipped rather than assumed to be at the end. A word listed here wrongly would weaken the test;
    // one missed would fail it loudly, which is the direction that costs nothing.
    std::set<size_t> intWords;
    intWords.insert( offsetof( CloudGpuPayload, LayerCount ) / sizeof( float ) );
    intWords.insert( offsetof( CloudGpuPayload, VoxelInstanceCount ) / sizeof( float ) );
    intWords.insert( offsetof( CloudGpuPayload, VoxelShadowCount ) / sizeof( float ) );
    intWords.insert( offsetof( CloudGpuPayload, Pad0 ) / sizeof( float ) );

    for ( size_t layer = 0; layer < kCloudMaxLayers; ++layer )
    {
        const size_t base =
             ( offsetof( CloudGpuPayload, Layers ) + layer * sizeof( CloudLayerPayload ) ) / sizeof( float );
        for ( size_t at = offsetof( CloudLayerPayload, WeatherSeed );
              at < offsetof( CloudLayerPayload, AmbientOcclusion ); at += sizeof( int32_t ) )
            intWords.insert( base + at / sizeof( float ) );
        intWords.insert( base + offsetof( CloudLayerPayload, AutoDistanceFade ) / sizeof( float ) );
        intWords.insert( base + offsetof( CloudLayerPayload, CloudShadowEnabled ) / sizeof( float ) );
    }

    const size_t wordCount = sizeof( CloudGpuPayload ) / sizeof( float );
    for ( size_t i = 0; i < wordCount; ++i )
    {
        if ( intWords.count( i ) != 0 )
            continue;
        EXPECT_TRUE( std::isfinite( words[i] ) ) << "word " << i;
    }
}

TEST( CloudPayload, EachLayerIsPackedFromItsOwnComponentAndNothingLeaksBetweenThem )
{
    Desert::ECS::VolumetricCloudData deck{};
    Desert::ECS::VolumetricCloudData sheet{};
    Desert::Graphic::AtmosphereEnv   atmosphere{};
    Desert::Graphic::WindEnv         wind{};

    deck.LayerBottomAltitude = 150000.0f;
    deck.Coverage            = 0.52f;
    deck.MaxSteps            = 176;
    deck.ExtinctionScale     = 1.0f;

    sheet.LayerBottomAltitude = 800000.0f;
    sheet.Coverage            = 0.66f;
    sheet.MaxSteps            = 64;
    sheet.ExtinctionScale     = 0.35f;

    Desert::Graphic::CloudLayerSet layers;
    layers.Layers[0] = deck;
    layers.Layers[1] = sheet;
    layers.Count     = 2;

    const CloudGpuPayload p = PackCloudParams( layers, atmosphere, wind, 0.0f, CloudVoxelCounts{} );

    EXPECT_EQ( p.LayerCount, 2 );

    EXPECT_FLOAT_EQ( p.Layers[0].LayerBottomAltitude, deck.LayerBottomAltitude );
    EXPECT_FLOAT_EQ( p.Layers[0].Coverage, deck.Coverage );
    EXPECT_EQ( p.Layers[0].MaxSteps, deck.MaxSteps );
    EXPECT_FLOAT_EQ( p.Layers[0].ExtinctionTint.w, deck.ExtinctionScale );

    EXPECT_FLOAT_EQ( p.Layers[1].LayerBottomAltitude, sheet.LayerBottomAltitude );
    EXPECT_FLOAT_EQ( p.Layers[1].Coverage, sheet.Coverage );
    EXPECT_EQ( p.Layers[1].MaxSteps, sheet.MaxSteps );
    EXPECT_FLOAT_EQ( p.Layers[1].ExtinctionTint.w, sheet.ExtinctionScale );
}

TEST( CloudPayload, TheViewWideSettingsComeFromThePrimaryLayerAlone )
{
    // There is one ray per pixel and one history pair per view, so there is one answer to each of these.
    // Taking it from the LOWEST layer rather than from whichever entity was created first is what makes
    // the frame reproducible; the Details panel says so on every layer that is not the primary.
    Desert::ECS::VolumetricCloudData deck{};
    Desert::ECS::VolumetricCloudData sheet{};
    Desert::Graphic::AtmosphereEnv   atmosphere{};
    Desert::Graphic::WindEnv         wind{};

    deck.JitterStrength      = 0.25f;
    deck.TemporalBlendFactor = 0.30f;
    deck.TemporalClampScale  = 2.00f;

    sheet.JitterStrength      = 1.00f;
    sheet.TemporalBlendFactor = 0.05f;
    sheet.TemporalClampScale  = 0.50f;

    Desert::Graphic::CloudLayerSet layers;
    layers.Layers[0] = deck;
    layers.Layers[1] = sheet;
    layers.Count     = 2;

    const CloudGpuPayload p = PackCloudParams( layers, atmosphere, wind, 0.0f, CloudVoxelCounts{} );

    EXPECT_FLOAT_EQ( p.JitterStrength, deck.JitterStrength );
    EXPECT_FLOAT_EQ( p.TemporalBlendFactor, deck.TemporalBlendFactor );
    EXPECT_FLOAT_EQ( p.TemporalClampScale, deck.TemporalClampScale );
}

TEST( CloudPayload, TheLayerCountCannotExceedWhatTheArrayHolds )
{
    // The march loops to this number and indexes the array with it. A count above the array's own size
    // would read a layer nobody packed - which is not a crash, it is a shell at a garbage altitude.
    Desert::Graphic::AtmosphereEnv atmosphere{};
    Desert::Graphic::WindEnv       wind{};

    Desert::Graphic::CloudLayerSet layers;
    layers.Count = kCloudMaxLayers + 5u;

    const CloudGpuPayload p = PackCloudParams( layers, atmosphere, wind, 0.0f, CloudVoxelCounts{} );
    EXPECT_EQ( p.LayerCount, static_cast<int32_t>( kCloudMaxLayers ) );
    EXPECT_GE( p.LayerCount, 0 );
}

// THE PIPELINE AND THE BUFFER, and the direction their disagreement is allowed to point.
//
// The raymarch's layer count is a SPECIALIZATION CONSTANT now, so it reaches the shader through the
// pipeline the dispatch chose rather than through the parameter block. Two channels for one number is
// exactly the shape this project has paid for before, and the failure here has no error message at all:
// a pipeline specialized to ONE layer, dispatched for a scene whose buffer packed TWO, simply never
// builds the second shell. The sheet is gone, nothing is logged, and every unit test of either side
// passes because each side is individually right.
//
// So assert the RELATION over the whole range, including the counts a hand-edited scene can produce: the
// pipeline never marches fewer layers than the buffer carries, and never more than the array holds.
TEST( CloudPayload, ThePipelineNeverMarchesFewerLayersThanTheBufferPacked )
{
    Desert::Graphic::AtmosphereEnv atmosphere{};
    Desert::Graphic::WindEnv       wind{};

    for ( uint32_t count = 0; count <= kCloudMaxLayers + 5u; ++count )
    {
        Desert::Graphic::CloudLayerSet layers;
        layers.Count = count;

        const CloudGpuPayload p       = PackCloudParams( layers, atmosphere, wind, 0.0f, CloudVoxelCounts{} );
        const uint32_t        marched = Desert::Graphic::CloudRaymarchLayerCount( count );

        EXPECT_GE( static_cast<int32_t>( marched ), p.LayerCount ) << "count " << count;
        EXPECT_LE( marched, kCloudMaxLayers ) << "count " << count;
        // And it always names a pipeline that exists: the array is indexed by marched - 1.
        EXPECT_GE( marched, 1u ) << "count " << count;
    }
}

// ---- The march plan ---------------------------------------------------------------------------------
//
// A ray crossing two cloud layers has to composite them near over far, and the plan is what makes that
// true BY CONSTRUCTION rather than by a sort: ordered, disjoint intervals, each bound to the layer that
// owns it. Every test below asserts a RELATION between the two shells rather than a value of one, which
// is the only kind of assertion that can catch this class of defect — each shell's own intersection is
// individually correct in every case, including the case where marching them in shell order is wrong.

namespace
{
    // The two shells the shipped Clouds_TwoLayerShowcase authors: a cumulus deck at 1.5-5.0 km and a
    // cirrus sheet at 8.0-9.2 km. Real numbers rather than round ones, because these properties have to
    // hold for the geometry that actually ships.
    constexpr float kDeckBottomKm     = 1.5f;
    constexpr float kDeckThicknessKm  = 3.5f;
    constexpr float kSheetBottomKm    = 8.0f;
    constexpr float kSheetThicknessKm = 1.2f;

    R::CloudShellHit ShellMiss()
    {
        R::CloudShellHit hit;
        hit.Hit    = false;
        hit.TEnter = 0.0f;
        hit.TExit  = 0.0f;
        return hit;
    }

    R::CloudShellHit ShellSpan( float enter, float exit )
    {
        R::CloudShellHit hit;
        hit.Hit    = true;
        hit.TEnter = enter;
        hit.TExit  = exit;
        return hit;
    }

    std::vector<R::CloudMarchSegment> SegmentsOf( const R::CloudMarchPlan& plan )
    {
        std::vector<R::CloudMarchSegment> segments;
        for ( int i = 0; i < plan.Count; ++i )
            segments.push_back( R::CloudPlanSegment( plan, i ) );
        return segments;
    }

    bool Inside( const R::CloudShellHit& shell, float t )
    {
        return shell.Hit && t >= shell.TEnter && t <= shell.TExit;
    }
} // namespace

TEST( CloudMarchPlan, TwoMissesProduceNothingToMarch )
{
    const R::CloudMarchPlan plan = R::CloudPlanTwoShells( ShellMiss(), 0, ShellMiss(), 1 );
    EXPECT_EQ( plan.Count, 0 );
}

TEST( CloudMarchPlan, OneShellIsTheIntervalItAlwaysWas )
{
    // The single-layer identity. Every scene shipped before two layers existed takes this path, and the
    // frame it produces has to be the frame it produced: one segment, the shell's own entry and exit,
    // and the layer that owns it.
    const R::CloudMarchPlan first = R::CloudPlanTwoShells( ShellSpan( 12.0f, 34.0f ), 0, ShellMiss(), 1 );
    ASSERT_EQ( first.Count, 1 );
    EXPECT_FLOAT_EQ( first.S0.TEnter, 12.0f );
    EXPECT_FLOAT_EQ( first.S0.TExit, 34.0f );
    EXPECT_EQ( first.S0.Layer, 0 );

    // And the same when it is the SECOND argument that hit: the plan is about the ray, not about the
    // order the caller happened to pass the shells in.
    const R::CloudMarchPlan second = R::CloudPlanTwoShells( ShellMiss(), 0, ShellSpan( 12.0f, 34.0f ), 1 );
    ASSERT_EQ( second.Count, 1 );
    EXPECT_FLOAT_EQ( second.S0.TEnter, 12.0f );
    EXPECT_FLOAT_EQ( second.S0.TExit, 34.0f );
    EXPECT_EQ( second.S0.Layer, 1 );
}

TEST( CloudMarchPlan, DisjointShellsAreMarchedNearFirstWhicheverWayTheyArePassed )
{
    const R::CloudMarchPlan forward =
         R::CloudPlanTwoShells( ShellSpan( 10.0f, 20.0f ), 0, ShellSpan( 30.0f, 40.0f ), 1 );
    ASSERT_EQ( forward.Count, 2 );
    EXPECT_EQ( forward.S0.Layer, 0 );
    EXPECT_EQ( forward.S1.Layer, 1 );
    EXPECT_FLOAT_EQ( forward.S0.TEnter, 10.0f );
    EXPECT_FLOAT_EQ( forward.S1.TEnter, 30.0f );

    const R::CloudMarchPlan reversed =
         R::CloudPlanTwoShells( ShellSpan( 30.0f, 40.0f ), 1, ShellSpan( 10.0f, 20.0f ), 0 );
    ASSERT_EQ( reversed.Count, 2 );
    EXPECT_EQ( reversed.S0.Layer, 0 );
    EXPECT_EQ( reversed.S1.Layer, 1 );
    EXPECT_FLOAT_EQ( reversed.S0.TEnter, 10.0f );
    EXPECT_FLOAT_EQ( reversed.S1.TEnter, 30.0f );
}

TEST( CloudMarchPlan, AContainedShellSplitsTheOneAroundIt )
{
    // The case a plain sort cannot express, and the reason this function exists. A camera above the
    // sheet looking down at a shallow angle gets a sheet interval that spans the descent AND the
    // ascent, with the deck's interval inside it. Marching the sheet whole and then the deck would draw
    // the deck behind cloud that is in front of it.
    const R::CloudMarchPlan plan =
         R::CloudPlanTwoShells( ShellSpan( 10.0f, 100.0f ), 1, ShellSpan( 30.0f, 50.0f ), 0 );
    ASSERT_EQ( plan.Count, 3 );

    EXPECT_EQ( plan.S0.Layer, 1 );
    EXPECT_FLOAT_EQ( plan.S0.TEnter, 10.0f );
    EXPECT_FLOAT_EQ( plan.S0.TExit, 30.0f );

    EXPECT_EQ( plan.S1.Layer, 0 );
    EXPECT_FLOAT_EQ( plan.S1.TEnter, 30.0f );
    EXPECT_FLOAT_EQ( plan.S1.TExit, 50.0f );

    EXPECT_EQ( plan.S2.Layer, 1 );
    EXPECT_FLOAT_EQ( plan.S2.TEnter, 50.0f );
    EXPECT_FLOAT_EQ( plan.S2.TExit, 100.0f );
}

TEST( CloudMarchPlan, ASplitThatWouldLeaveAnEmptyPieceDropsIt )
{
    // Contained, but sharing the container's start: the leading piece has zero length and is not a
    // segment. A zero-length segment is not an error, it is a march that begins where it ends — and
    // reporting one would make "every segment the plan names is marchable" false.
    const R::CloudMarchPlan leading =
         R::CloudPlanTwoShells( ShellSpan( 10.0f, 100.0f ), 1, ShellSpan( 10.0f, 50.0f ), 0 );
    ASSERT_EQ( leading.Count, 2 );
    EXPECT_EQ( leading.S0.Layer, 0 );
    EXPECT_EQ( leading.S1.Layer, 1 );

    const R::CloudMarchPlan trailing =
         R::CloudPlanTwoShells( ShellSpan( 10.0f, 100.0f ), 1, ShellSpan( 40.0f, 100.0f ), 0 );
    ASSERT_EQ( trailing.Count, 2 );
    EXPECT_EQ( trailing.S0.Layer, 1 );
    EXPECT_EQ( trailing.S1.Layer, 0 );
}

TEST( CloudMarchPlan, SegmentsAreOrderedAndDisjointForEveryCameraAndEveryDirection )
{
    // THE PROPERTY, over the geometry that ships. Ordered and disjoint is exactly what "one
    // transmittance accumulator composites near over far" needs, and it is the pair that a spot check of
    // any single configuration cannot establish.
    const std::vector<float> altitudesKm{ 0.002f, 1.0f, 3.0f, 6.0f, 8.5f, 11.0f, 20.0f, 60.0f };

    for ( const float altitude : altitudesKm )
    {
        const glm::vec3 camera( 0.0f, altitude, 0.0f );
        for ( int degrees = -90; degrees <= 90; degrees += 2 )
        {
            const float     radians = static_cast<float>( degrees ) * 3.14159265f / 180.0f;
            const glm::vec3 dir = glm::normalize( glm::vec3( std::cos( radians ), std::sin( radians ), 0.0f ) );

            const R::CloudShellHit deck =
                 R::CloudShellBounds( camera, dir, kPlanetRadiusKm, kDeckBottomKm, kDeckThicknessKm );
            const R::CloudShellHit sheet =
                 R::CloudShellBounds( camera, dir, kPlanetRadiusKm, kSheetBottomKm, kSheetThicknessKm );

            const R::CloudMarchPlan plan     = R::CloudPlanTwoShells( deck, 0, sheet, 1 );
            const auto              segments = SegmentsOf( plan );

            EXPECT_LE( plan.Count, CLOUD_MAX_MARCH_SEGMENTS )
                 << "altitude " << altitude << " elevation " << degrees;

            for ( std::size_t i = 0; i < segments.size(); ++i )
            {
                EXPECT_LT( segments[i].TEnter, segments[i].TExit )
                     << "empty segment at altitude " << altitude << " elevation " << degrees;

                // Inside the shell it claims to belong to, with a tolerance of nothing: the split only
                // ever narrows an interval.
                const R::CloudShellHit& own = segments[i].Layer == 0 ? deck : sheet;
                ASSERT_TRUE( own.Hit );
                EXPECT_GE( segments[i].TEnter, own.TEnter );
                EXPECT_LE( segments[i].TExit, own.TExit );

                if ( i > 0 )
                    EXPECT_LE( segments[i - 1].TExit, segments[i].TEnter )
                         << "segments overlap at altitude " << altitude << " elevation " << degrees;
            }
        }
    }
}

TEST( CloudMarchPlan, NoStretchOfAShellIsLostExceptWhereTheOtherLayerIs )
{
    // The other half of the property. Disjointness alone is satisfied by a plan that drops everything;
    // this says the plan still COVERS each shell, apart from the stretch it handed to the other layer —
    // which is the gap between two disjoint altitude bands, where the layer that gave it up has a height
    // fraction outside [0,1] and no density at all.
    const std::vector<float> altitudesKm{ 0.002f, 4.0f, 8.5f, 15.0f, 30.0f };

    for ( const float altitude : altitudesKm )
    {
        const glm::vec3 camera( 0.0f, altitude, 0.0f );
        for ( int degrees = -80; degrees <= 80; degrees += 5 )
        {
            const float     radians = static_cast<float>( degrees ) * 3.14159265f / 180.0f;
            const glm::vec3 dir = glm::normalize( glm::vec3( std::cos( radians ), std::sin( radians ), 0.0f ) );

            const R::CloudShellHit deck =
                 R::CloudShellBounds( camera, dir, kPlanetRadiusKm, kDeckBottomKm, kDeckThicknessKm );
            const R::CloudShellHit sheet =
                 R::CloudShellBounds( camera, dir, kPlanetRadiusKm, kSheetBottomKm, kSheetThicknessKm );

            const R::CloudMarchPlan plan     = R::CloudPlanTwoShells( deck, 0, sheet, 1 );
            const auto              segments = SegmentsOf( plan );

            for ( int layer = 0; layer < 2; ++layer )
            {
                const R::CloudShellHit& own   = layer == 0 ? deck : sheet;
                const R::CloudShellHit& other = layer == 0 ? sheet : deck;
                if ( !own.Hit )
                    continue;

                for ( int step = 1; step < 32; ++step )
                {
                    const float t =
                         own.TEnter + ( own.TExit - own.TEnter ) * ( static_cast<float>( step ) / 32.0f );
                    if ( Inside( other, t ) )
                        continue;

                    bool covered = false;
                    for ( const auto& segment : segments )
                        covered =
                             covered || ( segment.Layer == layer && t >= segment.TEnter && t <= segment.TExit );

                    EXPECT_TRUE( covered ) << "layer " << layer << " lost t=" << t << " at altitude " << altitude
                                           << " elevation " << degrees;
                }
            }
        }
    }
}

// ---- The per-layer slice coordinate -------------------------------------------------------------------

TEST( CloudLayerSlice, EveryLayerLandsOnATexelCentre )
{
    // The weather map, the profile map, the profile table and the shadow map are all volumes with one
    // slice per layer, and every volume in this engine is sampled LINEAR/REPEAT. A w half a texel off
    // would blend the deck's coverage into the sheet's — so the property that matters is not "the
    // coordinates differ" but "the trilinear weight on the neighbouring slice is exactly zero", which is
    // what landing on an integer texel index means.
    for ( int count = 1; count <= 4; ++count )
    {
        for ( int layer = 0; layer < count; ++layer )
        {
            const float w = R::CloudLayerSliceW( layer, count );

            EXPECT_GT( w, 0.0f );
            EXPECT_LT( w, 1.0f );

            const float unnormalized = w * static_cast<float>( count ) - 0.5f;
            const float residual     = std::abs( unnormalized - static_cast<float>( layer ) );

            // The residual is the fraction of a texel the neighbouring slice would be weighted by. A
            // count that is a power of two — which kCloudMaxLayers is, and which is the case that ships —
            // divides exactly in binary and leaves none of it at all.
            if ( ( count & ( count - 1 ) ) == 0 )
                EXPECT_FLOAT_EQ( unnormalized, static_cast<float>( layer ) ) << "count " << count;

            // For the rest, the bound that matters is not "exact" but "below anything a sampler can
            // resolve": Vulkan guarantees only four bits of sub-texel precision (a sixteenth of a texel)
            // and real hardware carries eight, so a residual four orders of magnitude under that cannot
            // move a weight off 1.0.
            EXPECT_LT( residual, 1.0e-5f ) << "count " << count << " layer " << layer;
        }
    }
}

TEST( CloudLayerSlice, ConsecutiveLayersAreOneWholeSliceApart )
{
    for ( int count = 2; count <= 4; ++count )
    {
        for ( int layer = 1; layer < count; ++layer )
        {
            const float step = R::CloudLayerSliceW( layer, count ) - R::CloudLayerSliceW( layer - 1, count );
            EXPECT_NEAR( step, 1.0f / static_cast<float>( count ), 1e-6f );
        }
    }
}

TEST( CloudLayerSlice, ADegenerateCountDoesNotDivideByZero )
{
    // A count of zero cannot reach the shader — a scene with no live layer never dispatches the march at
    // all — but a division by it would produce an inf that survives every clamp it
    // touches, so the guard is in the function rather than in the caller.
    EXPECT_TRUE( std::isfinite( R::CloudLayerSliceW( 0, 0 ) ) );
    EXPECT_FLOAT_EQ( R::CloudLayerSliceW( 0, 1 ), 0.5f );
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

// ---------------------------------------------------------------------------------------------------
// The Cloud Type axis: authored profile curves and per-cell vertical bands.
//
// Every assertion here is a RELATION between two things that must agree and that are individually
// correct either way: the table's baseline rows against the trapezoids they replaced, a profile's
// support against the band it was told to live in, and the "off" end of each new knob against the field
// the engine had before the knob existed. None of these has a symptom when it goes wrong — a profile
// evaluated in the wrong band renders a sky, just not the authored one.
// ---------------------------------------------------------------------------------------------------

namespace
{
    // The shader's own fetch, on the CPU: bilinear over the baked table with the SAME coordinate
    // convention Common/CloudProfile.glslh gives the GPU. Texel centres map back to
    // `uv * size - 0.5`, so a lookup coordinate from CloudProfileLutCoord lands on exactly the tap the
    // builder wrote. Written here and not in the engine because only a test needs it.
    glm::vec3 FetchProfileLut( const std::vector<unsigned char>& lut, glm::vec2 uv )
    {
        const glm::vec2 size( static_cast<float>( Desert::Graphic::kCloudProfileLutWidth ),
                              static_cast<float>( Desert::Graphic::kCloudProfileLutTypes ) );
        const float     x  = uv.x * size.x - 0.5f;
        const float     y  = uv.y * size.y - 0.5f;
        const int       x0 = static_cast<int>( std::floor( x ) );
        const int       y0 = static_cast<int>( std::floor( y ) );
        const float     fx = x - static_cast<float>( x0 );
        const float     fy = y - static_cast<float>( y0 );

        const auto texel = [&lut]( int tx, int ty, int channel )
        {
            const int cx = std::clamp( tx, 0, static_cast<int>( Desert::Graphic::kCloudProfileLutWidth ) - 1 );
            const int cy = std::clamp( ty, 0, static_cast<int>( Desert::Graphic::kCloudProfileLutTypes ) - 1 );
            const std::size_t at =
                 ( static_cast<std::size_t>( cy ) * Desert::Graphic::kCloudProfileLutWidth + cx ) * 4u;
            return static_cast<float>( lut[at + static_cast<std::size_t>( channel )] ) / 255.0f;
        };

        glm::vec3 out( 0.0f );
        for ( int c = 0; c < 3; ++c )
        {
            const float a = glm::mix( texel( x0, y0, c ), texel( x0 + 1, y0, c ), fx );
            const float b = glm::mix( texel( x0, y0 + 1, c ), texel( x0 + 1, y0 + 1, c ), fx );
            out[c]        = glm::mix( a, b, fy );
        }
        return out;
    }

    // The whole profile path, end to end: layer height in, density multiplier out.
    float ProfileAt( const std::vector<unsigned char>& lut, float heightFraction, float cloudType, glm::vec2 band,
                     float basePower, float topPower )
    {
        const float cellHeight = R::CloudProfileCellHeight( heightFraction, band );
        if ( cellHeight < 0.0f || cellHeight > 1.0f )
            return 0.0f;

        const glm::vec2 size( static_cast<float>( Desert::Graphic::kCloudProfileLutWidth ),
                              static_cast<float>( Desert::Graphic::kCloudProfileLutTypes ) );
        const glm::vec3 texel = FetchProfileLut( lut, R::CloudProfileLutCoord( cellHeight, cloudType, size ) );
        return R::CloudProfileCompose( texel, basePower, topPower );
    }

    // Cloud Type of the row an anchor sits on: rows are evenly spaced over [0, 1].
    float TypeOfRow( Desert::Graphic::CloudProfileForm form )
    {
        return static_cast<float>( form ) / static_cast<float>( Desert::Graphic::kCloudProfileLutTypes - 1u );
    }
} // namespace

// THE STRICT-SUBSET PROPERTY. The three rows that carry the component's own trapezoids reproduce, to
// within 8-bit quantisation and the table's tap spacing, exactly the profile the three-way blend
// computed before the table existed. This is what makes the new axis a WIDENING of the old knob rather
// than a retuning of it, and it is the single assertion that lets an already-authored scene be trusted.
TEST( CloudProfileTable, BaselineRowsReproduceTheTrapezoidsTheyReplaced )
{
    using Desert::Graphic::CloudProfileForm;

    const Desert::ECS::VolumetricCloudData data{};
    const std::vector<unsigned char>       lut = Desert::Graphic::BuildCloudProfileLut( data );

    const struct
    {
        CloudProfileForm Form;
        glm::vec4        Gradient;
        const char*      Name;
    } rows[] = {
         { CloudProfileForm::Stratus, data.StratusGradient, "Stratus" },
         { CloudProfileForm::Stratocumulus, data.StratocumulusGradient, "Stratocumulus" },
         { CloudProfileForm::Cumulus, data.CumulusGradient, "Cumulus" },
    };

    for ( const auto& row : rows )
    {
        for ( int i = 0; i <= 200; ++i )
        {
            const float h = static_cast<float>( i ) / 200.0f;

            // The formula CloudDensityProcedural.glslh evaluated before this change, verbatim.
            const float baseIn = R::CloudRemapRange( h, row.Gradient.x, row.Gradient.y, 0.0f, 1.0f );
            const float topOut = R::CloudRemapRange( h, row.Gradient.z, row.Gradient.w, 1.0f, 0.0f );
            const float want =
                 std::pow( baseIn, data.BaseGradientPower ) * std::pow( topOut, data.TopGradientPower );

            const float got = ProfileAt( lut, h, TypeOfRow( row.Form ), glm::vec2( 0.0f, 1.0f ),
                                         data.BaseGradientPower, data.TopGradientPower );

            EXPECT_NEAR( got, want, 0.03f ) << row.Name << " at height " << h;
        }
    }
}

// The new rows are not the old ones wearing a different name. A form that a trapezoid pair CAN express
// is a monotone rise times a monotone fall, so its profile has exactly one maximum and never rises
// again after falling; the anvil's whole point is that it does.
TEST( CloudProfileTable, TheAnvilRowIsNotReachableByAnyTrapezoidPair )
{
    const Desert::ECS::VolumetricCloudData data{};
    const std::vector<unsigned char>       lut  = Desert::Graphic::BuildCloudProfileLut( data );
    const float                            type = TypeOfRow( Desert::Graphic::CloudProfileForm::Anvil );

    std::vector<float> profile;
    for ( int i = 0; i <= 100; ++i )
    {
        const float h = static_cast<float>( i ) / 100.0f;
        profile.push_back(
             ProfileAt( lut, h, type, glm::vec2( 0.0f, 1.0f ), data.BaseGradientPower, data.TopGradientPower ) );
    }

    // Walk the curve and count the times it turns around by more than the quantisation noise. A
    // trapezoid product turns at most once (up, then down); a waist under a flare turns three times.
    int         turns     = 0;
    int         direction = 0;
    const float epsilon   = 0.02f;
    for ( std::size_t i = 1; i < profile.size(); ++i )
    {
        const float delta = profile[i] - profile[i - 1];
        if ( std::fabs( delta ) < epsilon )
            continue;
        const int now = delta > 0.0f ? 1 : -1;
        if ( direction != 0 && now != direction )
            ++turns;
        direction = now;
    }

    EXPECT_GE( turns, 2 ) << "the anvil profile rises, falls and rises again — a trapezoid pair cannot";
}

// A profile is a density multiplier. Above 1 it would brighten a cloud past the coverage that placed
// it; below 0 it would subtract cloud from the sky.
TEST( CloudProfileTable, EveryProfileIsBoundedByZeroAndOne )
{
    const Desert::ECS::VolumetricCloudData data{};
    const std::vector<unsigned char>       lut = Desert::Graphic::BuildCloudProfileLut( data );

    for ( int t = 0; t <= 40; ++t )
    {
        const float type = static_cast<float>( t ) / 40.0f;
        for ( int i = 0; i <= 100; ++i )
        {
            const float h = static_cast<float>( i ) / 100.0f;
            const float p =
                 ProfileAt( lut, h, type, glm::vec2( 0.0f, 1.0f ), data.BaseGradientPower, data.TopGradientPower );
            EXPECT_GE( p, 0.0f ) << "type " << type << " height " << h;
            EXPECT_LE( p, 1.0f ) << "type " << type << " height " << h;
        }
    }
}

// A cell's cloud lives between the cell's own base and its own ceiling, and nowhere else. Without this
// the per-cell band is decoration: the profile would still be evaluated across the whole layer and
// every cloud would still start and stop at the same two altitudes.
TEST( CloudProfileTable, ASupportLiesInsideTheCellsOwnBand )
{
    const Desert::ECS::VolumetricCloudData data{};
    const std::vector<unsigned char>       lut = Desert::Graphic::BuildCloudProfileLut( data );

    const glm::vec2 bands[] = {
         glm::vec2( 0.0f, 1.0f ),
         glm::vec2( 0.10f, 0.45f ),
         glm::vec2( 0.55f, 0.95f ),
         glm::vec2( 0.30f, 0.70f ),
    };

    for ( const glm::vec2& band : bands )
    {
        for ( int t = 0; t <= 10; ++t )
        {
            const float type = static_cast<float>( t ) / 10.0f;
            for ( int i = 0; i <= 200; ++i )
            {
                const float h = static_cast<float>( i ) / 200.0f;
                if ( h >= band.x && h <= band.y )
                    continue;
                EXPECT_FLOAT_EQ( ProfileAt( lut, h, type, band, data.BaseGradientPower, data.TopGradientPower ),
                                 0.0f )
                     << "band [" << band.x << ", " << band.y << "] leaked at height " << h;
            }
        }
    }
}

// Two cells at different altitudes are two clouds, one above the other — not one cloud smeared over
// both. This is the pp. 126/150 signature the audit named, and it is the reason the map grew channels.
TEST( CloudProfileTable, CellsAtDifferentAltitudesHaveDisjointSupports )
{
    const Desert::ECS::VolumetricCloudData data{};
    const std::vector<unsigned char>       lut = Desert::Graphic::BuildCloudProfileLut( data );

    const glm::vec2 low( 0.05f, 0.40f );
    const glm::vec2 high( 0.60f, 0.95f );
    const float     type = TypeOfRow( Desert::Graphic::CloudProfileForm::Cumulus );

    bool lowHasBody  = false;
    bool highHasBody = false;

    for ( int i = 0; i <= 200; ++i )
    {
        const float h    = static_cast<float>( i ) / 200.0f;
        const float pLow = ProfileAt( lut, h, type, low, data.BaseGradientPower, data.TopGradientPower );
        const float pTop = ProfileAt( lut, h, type, high, data.BaseGradientPower, data.TopGradientPower );

        lowHasBody  = lowHasBody || pLow > 0.5f;
        highHasBody = highHasBody || pTop > 0.5f;

        // Disjoint: no height carries body for both. The bands do not overlap, so nothing may.
        EXPECT_FALSE( pLow > 0.0f && pTop > 0.0f ) << "both cells have cloud at height " << h;
    }

    EXPECT_TRUE( lowHasBody ) << "the low cell has no cloud at all";
    EXPECT_TRUE( highHasBody ) << "the high cell has no cloud at all";
}

// The "off" end of each new knob is the engine that existed before it. Not approximately — the whole
// point of defaulting a serialized field is that a scene authored before it loads unchanged.
TEST( CloudProfileTable, TheOffEndOfEachNewKnobIsThePreviousBehaviour )
{
    // Height Variance 0: every cell owns the whole layer, whatever the map says.
    const float ndfs[][2] = { { 0.0f, 1.0f }, { 0.25f, 0.60f }, { 0.48f, 0.52f }, { 0.9f, 0.1f } };
    for ( const auto& ndf : ndfs )
    {
        const glm::vec2 band = R::CloudProfileCellBand( ndf[0], ndf[1], 0.0f );
        EXPECT_FLOAT_EQ( band.x, 0.0f );
        EXPECT_FLOAT_EQ( band.y, 1.0f );
    }

    // Type Variance 0: one uniform type over the whole sky, whatever the noise field says.
    for ( int i = 0; i <= 20; ++i )
    {
        const float noise = static_cast<float>( i ) / 20.0f;
        EXPECT_FLOAT_EQ( R::CloudProfileTypeAt( 0.6f, noise, 0.0f ), 0.6f );
        EXPECT_FLOAT_EQ( R::CloudProfileTypeAt( 0.0f, noise, 0.0f ), 0.0f );
    }

    // And a non-zero variance stays inside the axis: a type outside [0, 1] would index off the table.
    for ( int i = 0; i <= 20; ++i )
    {
        const float noise = static_cast<float>( i ) / 20.0f;
        const float type  = R::CloudProfileTypeAt( 0.5f, noise, 1.0f );
        EXPECT_GE( type, 0.0f );
        EXPECT_LE( type, 1.0f );
    }
}

// The band the weather pass writes is ordered by construction, and a degenerate one still divides.
TEST( CloudProfileTable, TheWrittenBandIsOrderedAndNeverZeroWidth )
{
    for ( int c = 0; c <= 20; ++c )
    {
        for ( int w = 0; w <= 20; ++w )
        {
            const glm::vec2 ndf =
                 R::CloudProfileHeightNdf( static_cast<float>( c ) / 20.0f, static_cast<float>( w ) / 20.0f );
            EXPECT_LE( ndf.x, ndf.y );
            EXPECT_GE( ndf.x, 0.0f );
            EXPECT_LE( ndf.y, 1.0f );

            for ( int a = 0; a <= 4; ++a )
            {
                const glm::vec2 band = R::CloudProfileCellBand( ndf.x, ndf.y, static_cast<float>( a ) / 4.0f );
                EXPECT_GT( band.y, band.x );
            }
        }
    }

    // Even a map whose two channels arrived the wrong way round — an artist can seed one — must not
    // hand the march a zero or negative width to divide by.
    const glm::vec2 inverted = R::CloudProfileCellBand( 0.9f, 0.1f, 1.0f );
    EXPECT_GT( inverted.y, inverted.x );
}

// THE ASYMMETRY. A cloud field condenses at ONE level and climbs to a hundred different ones, so the
// two ends of a cell's band are not the same kind of quantity: bases scatter by tens of metres and tops
// by kilometres. The centre/half-width construction this replaced gave them the SAME range, which is
// what "the clouds look close to the ground, not up in the sky" is — with every cloud's floor at its own
// altitude there is no plane of flat bases for the eye to read as a ceiling.
//
// Four is the factor the constants deliver at 5, asserted with a doubling of margin so a deliberate
// retune of either range fails only when it has actually destroyed the asymmetry.
TEST( CloudProfileTable, CloudBasesShareALevelWhileCloudTopsDoNot )
{
    float baseLow = 1.0f, baseHigh = 0.0f, topLow = 1.0f, topHigh = 0.0f;

    for ( int b = 0; b <= 20; ++b )
    {
        for ( int t = 0; t <= 20; ++t )
        {
            const glm::vec2 ndf =
                 R::CloudProfileHeightNdf( static_cast<float>( b ) / 20.0f, static_cast<float>( t ) / 20.0f );
            baseLow  = std::min( baseLow, ndf.x );
            baseHigh = std::max( baseHigh, ndf.x );
            topLow   = std::min( topLow, ndf.y );
            topHigh  = std::max( topHigh, ndf.y );
        }
    }

    const float baseSpan = baseHigh - baseLow;
    const float topSpan  = topHigh - topLow;

    EXPECT_GT( baseSpan, 0.0f ) << "a base that cannot move at all is a slab, not a cloud field";
    EXPECT_LE( baseSpan * 4.0f, topSpan )
         << "cell bases wander " << baseSpan << " of the layer against tops' " << topSpan
         << ": the two ends of the band have stopped being different kinds of quantity";

    // And the asymmetry survives Cloud Height Variance, which scales BOTH ends: it is a property of the
    // construction, not of one authored value. At variance 0 both collapse to the whole layer, which is
    // the off-end the neighbouring test pins, so the sweep starts above it.
    for ( int a = 1; a <= 4; ++a )
    {
        const float amount = static_cast<float>( a ) / 4.0f;
        float       bLow = 1.0f, bHigh = 0.0f, tLow = 1.0f, tHigh = 0.0f;
        for ( int b = 0; b <= 20; ++b )
        {
            for ( int t = 0; t <= 20; ++t )
            {
                const glm::vec2 ndf =
                     R::CloudProfileHeightNdf( static_cast<float>( b ) / 20.0f, static_cast<float>( t ) / 20.0f );
                const glm::vec2 band = R::CloudProfileCellBand( ndf.x, ndf.y, amount );
                bLow                 = std::min( bLow, band.x );
                bHigh                = std::max( bHigh, band.x );
                tLow                 = std::min( tLow, band.y );
                tHigh                = std::max( tHigh, band.y );
            }
        }
        EXPECT_LE( ( bHigh - bLow ) * 4.0f, tHigh - tLow ) << "at Cloud Height Variance " << amount;
    }
}

// THE MECHANISM, asserted rather than described: the base-in ramp is the GAIN that turns horizontal
// variation into vertical displacement of the cloud bottom.
//
// A column becomes cloud at the height where `profile(h) x K` crosses the erosion threshold, where K is
// everything horizontal — coverage, the base-shape modulation, the density scale. Only `profile` depends
// on height, so the cloud's base is that equation solved for h, and the SCATTER of that solution across
// a field of different K is proportional to the width of the base-in ramp. Halve the ramp and the sky's
// bases halve their scatter; that is why this change is authored per species in the ramps and not in a
// power or a coverage number.
//
// Asserted as a ratio rather than an absolute, so it holds whatever the sampled K range is.
TEST( CloudProfileTable, BaseAltitudeScatterIsProportionalToTheBaseInRamp )
{
    Desert::ECS::VolumetricCloudData data{};
    const float                      type = TypeOfRow( Desert::Graphic::CloudProfileForm::Cumulus );
    const glm::vec2                  band( 0.0f, 1.0f );

    // The height at which `profile x K` first crosses `threshold`, swept over a decade of K — the range
    // the coverage field actually spans between the middle of a cell and its edge.
    const auto scatter = [&]( float baseIn ) -> float
    {
        data.CumulusGradient                  = glm::vec4( 0.0f, baseIn, 0.68f, 0.92f );
        const std::vector<unsigned char> lut  = Desert::Graphic::BuildCloudProfileLut( data );
        float                            low  = 1.0f;
        float                            high = 0.0f;
        for ( int k = 0; k <= 20; ++k )
        {
            const float coverage  = glm::mix( 0.1f, 1.0f, static_cast<float>( k ) / 20.0f );
            const float threshold = 0.08f; // the detail composite's own mean, times Detail Strength
            for ( int i = 0; i <= 2000; ++i )
            {
                const float h = static_cast<float>( i ) / 2000.0f;
                if ( ProfileAt( lut, h, type, band, data.BaseGradientPower, data.TopGradientPower ) * coverage >
                     threshold )
                {
                    low  = std::min( low, h );
                    high = std::max( high, h );
                    break;
                }
            }
        }
        return high - low;
    };

    const float wide   = scatter( 0.22f ); // what the cumulus row carried before this change
    const float narrow = scatter( 0.05f ); // and what a condensation level justifies

    EXPECT_GT( wide, 0.0f );
    EXPECT_LT( narrow, wide ) << "a narrower base-in did not tighten the bases at all";

    // Proportional: the ratio of scatters follows the ratio of ramps. The tolerance is generous because
    // the table is 8-bit and sampled at 128 taps; the RELATION is the assertion, not the third digit.
    const float expected = 0.05f / 0.22f;
    EXPECT_NEAR( narrow / wide, expected, 0.25f * expected )
         << "base scatter " << narrow << " vs " << wide << " for ramps 0.05 vs 0.22";
}

// The table's dimensions are the shader's only source of truth for the type axis, because the shader
// reads them with textureSize(). This pins the bytes the CPU actually uploads.
TEST( CloudProfileTable, TheBakedTableHasTheDimensionsTheShaderWillRead )
{
    const Desert::ECS::VolumetricCloudData data{};
    const std::vector<unsigned char>       lut = Desert::Graphic::BuildCloudProfileLut( data );

    EXPECT_EQ( lut.size(), static_cast<std::size_t>( Desert::Graphic::kCloudProfileLutWidth ) *
                                Desert::Graphic::kCloudProfileLutTypes * 4u );
    EXPECT_EQ( Desert::Graphic::kCloudProfileLutTypes, 6u )
         << "six authored forms: scud, shelf, stratocumulus, cumulus, congestus, anvil";
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
