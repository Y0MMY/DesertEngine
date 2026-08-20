// The cloud shadow map, tested as the RELATION it is rather than as two functions that happen to exist.
//
// The subsystem's defect taxonomy (DEV_CONTRACT §2.3.1) is one shape repeated: two sides that must agree,
// each individually correct, and nothing asserting the agreement. This feature has four such pairs, and
// this suite is one assertion per pair:
//
//   1. THE ENCODE AND THE DECODE. One shader writes (frontDepthKm, meanExtinctionPerKm, maxOpticalDepth)
//      and a completely different one reads it back at a depth the writer never knew. If they disagree
//      the picture is a shadow of the wrong strength, which reads as a badly tuned extinction and gets
//      "fixed" by moving the extinction. The assertion is that the reconstructed transmittance equals the
//      Beer-Lambert integral the LIGHT MARCH computes along the same ray — which is the requirement in
//      the task, and the reason the medium here is synthetic: a slab has a closed-form answer.
//
//   2. THE C++ MIRROR AND THE SHADER'S OWN CONSTANTS. Graphic::CloudShadowPayload.hpp restates six
//      numbers and three functions that Common/CloudShadowMap.glslh owns. Both are compiled here and
//      compared, so a drifting mirror is a red test rather than a shadow in the wrong place.
//
//   3. THE MAP'S TEXEL AND THE MARCH'S OWN RESOLUTION. The extent was DERIVED from the finest cloud chord
//      the view march can find; if either side moves, the derivation is void. Asserted as the inequality
//      it is, against Common/CloudGeometry.glslh's own schedule.
//
//   4. THE SNAP AND THE FRAME BEFORE IT. The anti-shimmer property is not a property of one frame at all:
//      it is that two frames with the camera in different places produce the SAME matrix. That is
//      invisible in any single rendered frame by construction, and it is one line here.
//
// Everything in this file is GPU-free. Common/CloudShadowMap.glslh and Common/CloudGeometry.glslh are
// compiled AS C++ through CloudShadowReference.hpp, so every assertion is about the text the GPU runs.

#include "CloudShadowReference.hpp"

#include <Engine/Graphic/Clouds/CloudShadowPayload.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

using namespace Desert::Tests::CloudShadowRef;

namespace
{
    // The shipped layer of Clouds_Demo: a 6360 km planet with the built-in cumulus congestus' envelope,
    // 2.2 to 5.8 km. Written as numbers rather than read from the component because this suite is about
    // the map, not about the defaults — ComponentReflection owns those.
    constexpr float kPlanetKm = 6360.0f;
    constexpr float kBaseKm   = 2.2f;
    constexpr float kTopKm    = 5.8f;

    CloudLayer MakeLayer()
    {
        return CloudMakeLayer( kPlanetKm, kBaseKm, kTopKm - kBaseKm );
    }

    // The ray one texel of the map marches, expressed the way both the shader and this test need it.
    struct TexelRay
    {
        vec3  OriginKm;
        vec3  Direction;
        float EnterKm = 0.0f;
        float ExitKm  = 0.0f;
    };

    // A ray starting well above the layer and aimed at the planet, i.e. what a shadow-map texel always
    // is. @p tiltX moves it off vertical so the "sun is not overhead" cases are covered too.
    TexelRay MakeTexelRay( float startAltitudeKm, float tiltX )
    {
        CloudLayer layer = MakeLayer();

        TexelRay ray;
        ray.OriginKm  = vec3( 0.0f, kPlanetKm + startAltitudeKm, 0.0f );
        ray.Direction = glm::normalize( vec3( tiltX, -1.0f, 0.0f ) );

        const vec2 segment = CloudLayerIntersect( layer, ray.OriginKm, ray.Direction );
        ray.EnterKm        = segment.x;
        ray.ExitKm         = segment.y;
        return ray;
    }

    // WHAT THE LIGHT MARCH WOULD SAY, computed at a step fine enough that its own discretisation is not
    // what the comparison is measuring: the optical depth from the ray's origin to @p depthKm, integrated
    // over the SAME extinction function the texel was built from.
    //
    // This is the reference side of assertion 1. It is deliberately NOT the map's own accumulation — it
    // is the quantity the renderer computes the other way, by marching from the shaded point toward the
    // sun (CloudRaymarch.shader's CloudLightOpticalDepth), and the encoding's whole claim is that one
    // fetch reproduces it.
    double ReferenceOpticalDepth( const TexelRay& ray, float depthKm, int steps = 200000 )
    {
        const double dt  = static_cast<double>( depthKm ) / steps;
        double       tau = 0.0;
        for ( int i = 0; i < steps; ++i )
        {
            const double t   = ( i + 0.5 ) * dt;
            const vec3   pos = ray.OriginKm + ray.Direction * static_cast<float>( t );
            tau += static_cast<double>( CloudShadowTestExtinction( pos ) ) * dt;
        }
        return tau;
    }

    void SetUniformSlab( float extinctionPerKm )
    {
        g_Medium.BottomRadiusKm     = kPlanetKm + kBaseKm;
        g_Medium.TopRadiusKm        = kPlanetKm + kTopKm;
        g_Medium.ExtinctionPerKm    = extinctionPerKm;
        g_Medium.ModulationAmount   = 0.0f;
        g_Medium.ModulationPeriodKm = 1.0f;
    }
} // namespace

// ---------------------------------------------------------------------------------------------------
// 1. THE ENCODE AND THE DECODE — the relation this feature exists to get right
// ---------------------------------------------------------------------------------------------------

// A homogeneous slab is the case in which the encoding is not an approximation at all, so the assertion
// can be an equality rather than a tolerance. If this fails, the triple means something other than what
// the consumer thinks it means, and no amount of tuning the extinction will put the shadow right.
TEST( CloudShadowMap, ReconstructionMatchesTheMarchThroughAUniformSlab )
{
    SetUniformSlab( 8.0f ); // the component's shipped Extinction Scale

    const TexelRay         ray     = MakeTexelRay( 60.0f, 0.0f );
    const float            far     = CloudShadowFarDepthKm( CLOUD_SHADOWMAP_EXTENT_KM );
    const CloudShadowTexel data    = CloudShadowTraceRay( ray.OriginKm, ray.Direction, ray.EnterKm, ray.ExitKm,
                                                          static_cast<int>( CLOUD_SHADOWMAP_BASE_SAMPLES ), far );
    const vec3             encoded = CloudShadowEncode( data );

    // Sampled from above the cloud, at its front, through it, and out the far side onto the ground.
    const float thicknessKm = ray.ExitKm - ray.EnterKm;
    const float probes[]    = { ray.EnterKm - 5.0f,
                                ray.EnterKm,
                                ray.EnterKm + 0.25f * thicknessKm,
                                ray.EnterKm + 0.5f * thicknessKm,
                                ray.EnterKm + 0.9f * thicknessKm,
                                ray.ExitKm,
                                ray.ExitKm + 20.0f };

    for ( float depthKm : probes )
    {
        const double referenceTau  = ReferenceOpticalDepth( ray, depthKm );
        const float  reconstructed = CloudShadowTransmittance( encoded, depthKm, 1.0f );
        const double referenceT    = std::exp( -referenceTau );

        // 1e-3 of transmittance is two orders finer than anything a frame can show and an order finer
        // than the 8-bit target the shot is written to. The residual is the map's 32-sample quadrature
        // against the reference's 200 000.
        EXPECT_NEAR( reconstructed, static_cast<float>( referenceT ), 1e-3f )
             << "depth " << depthKm << " km along the ray";
    }
}

// WHERE THE ENCODING IS EXACT AND WHERE IT IS A LINEAR APPROXIMATION, stated as an assertion so that
// nobody has to discover it from a frame. Once the medium VARIES along the ray a single mean extinction
// cannot reproduce a profile, and this test pins which half of the picture that costs:
//
//   * A RECEIVER BELOW THE LAYER — the ground, which is what this whole feature is for — is EXACT. Its
//     depth is past the far side, the min() clamps to MaxOpticalDepth, and MaxOpticalDepth is the ray's
//     own integral, stored verbatim. The only residual is the 32-sample quadrature.
//   * A RECEIVER INSIDE THE LAYER gets the mean, which straightens the profile: it under-shades where the
//     medium is thinner than average and over-shades where it is denser. The error is BOUNDED by the
//     ray's total optical depth, which is what this asserts; it cannot invent material that is not on the
//     ray, only redistribute it along it.
//
// The medium here is deliberately brutal — a 0.9-amplitude modulation over a 0.4 km period inside a
// 3.6 km slab, far coarser structure than a real vertical profile — so the bound is measured against the
// worst case rather than against a flattering one.
TEST( CloudShadowMap, ExactBelowTheLayerAndBoundedInsideIt )
{
    SetUniformSlab( 8.0f );
    g_Medium.ModulationAmount   = 0.9f;
    g_Medium.ModulationPeriodKm = 0.4f;

    const TexelRay         ray     = MakeTexelRay( 60.0f, 0.35f );
    const float            far     = CloudShadowFarDepthKm( CLOUD_SHADOWMAP_EXTENT_KM );
    const CloudShadowTexel data    = CloudShadowTraceRay( ray.OriginKm, ray.Direction, ray.EnterKm, ray.ExitKm,
                                                          static_cast<int>( CLOUD_SHADOWMAP_BASE_SAMPLES ), far );
    const vec3             encoded = CloudShadowEncode( data );

    const double referenceTotal = ReferenceOpticalDepth( ray, ray.ExitKm );

    // THE GROUND CASE, and it is an equality. Two per cent is the 32-sample quadrature against the
    // reference's 200 000, not slack in the encoding.
    EXPECT_NEAR( data.MaxOpticalDepth, static_cast<float>( referenceTotal ), 0.02 * referenceTotal );

    const float belowKm = ray.ExitKm + 50.0f;
    EXPECT_NEAR( CloudShadowOpticalDepth( data, belowKm ), data.MaxOpticalDepth, 1e-5f );
    EXPECT_NEAR( CloudShadowTransmittance( encoded, belowKm, 1.0f ),
                 static_cast<float>( std::exp( -referenceTotal ) ),
                 0.02f * static_cast<float>( std::exp( -referenceTotal ) ) + 1e-6f );

    // Far below, the answer has stopped changing entirely: there is no more material to cross, and
    // without the min() the mean extinction would keep accumulating through the clear air under the
    // cloud until the ground went black at a distance rather than under a cloud.
    EXPECT_FLOAT_EQ( CloudShadowTransmittance( encoded, ray.ExitKm + 50.0f, 1.0f ),
                     CloudShadowTransmittance( encoded, far, 1.0f ) );

    // THE INSIDE CASE, and it is a bound. The worst pointwise error is printed so the number is on the
    // record rather than implied by a tolerance.
    float worstError = 0.0f;
    for ( int i = 0; i <= 40; ++i )
    {
        const float  depthKm = ray.EnterKm + ( ray.ExitKm - ray.EnterKm ) * ( i / 40.0f );
        const double refTau  = ReferenceOpticalDepth( ray, depthKm, 20000 );
        const float  mapTau  = CloudShadowOpticalDepth( data, depthKm );

        worstError = std::max( worstError, std::abs( mapTau - static_cast<float>( refTau ) ) );

        EXPECT_LE( mapTau, data.MaxOpticalDepth + 1e-4f )
             << "depth " << depthKm << " km: the map may redistribute material along the ray, never add it";
    }
    EXPECT_LE( worstError, static_cast<float>( referenceTotal ) );
    std::printf( "[cloud shadow] worst in-layer optical-depth error %.3f against a ray total of %.3f\n",
                 worstError, referenceTotal );
}

// THE MEAN IS OVER THE MATERIAL, NOT OVER THE CHORD — the one line of CloudShadowTraceRay that looks
// like tidiness and is not.
//
// `extinctionSum / max(1, hitCount)` counts only the samples that found something. Dividing by the SAMPLE
// COUNT instead is the obvious simplification, it is what a reader reaches for, and it is wrong in a way
// that gets worse the taller the shell is: the mean is multiplied by the depth travelled PAST THE FRONT of
// the cloud, so it has to be the extinction of the CLOUD and not of the whole chord including the clear air
// above and below it.
//
// This case is the one that separates them, and the suite did not have it until a deliberate breakage
// showed that every other test here passes with the wrong divisor: a THIN medium inside a TALL shell. Half
// a kilometre of cloud inside a 3.6 km envelope makes the two divisors differ by seven times, and the
// symptom in a frame would be a stratus deck that casts almost no shadow while a congestus filling the
// same shell casts a correct one — which reads as a problem with the stratus.
TEST( CloudShadowMap, TheMeanIsOverTheMaterialAndNotOverTheChord )
{
    SetUniformSlab( 8.0f );
    // The medium occupies 3.0 to 3.5 km of a shell that runs 2.2 to 5.8.
    g_Medium.BottomRadiusKm = kPlanetKm + 3.0f;
    g_Medium.TopRadiusKm    = kPlanetKm + 3.5f;

    const TexelRay         ray     = MakeTexelRay( 60.0f, 0.0f );
    const float            far     = CloudShadowFarDepthKm( CLOUD_SHADOWMAP_EXTENT_KM );
    const CloudShadowTexel data    = CloudShadowTraceRay( ray.OriginKm, ray.Direction, ray.EnterKm, ray.ExitKm,
                                                          static_cast<int>( CLOUD_SHADOWMAP_BASE_SAMPLES ), far );
    const vec3             encoded = CloudShadowEncode( data );

    // The stored mean is the medium's own extinction, not a seventh of it.
    EXPECT_NEAR( data.MeanExtinctionPerKm, 8.0f, 0.05f );

    // And the reconstruction is right at the BOTTOM OF THE CLOUD, which is where the two divisors part
    // company: at the shell's own exit both are clamped by MaxOpticalDepth and agree by accident.
    const float  bottomOfCloudKm = data.FrontDepthKm + 0.5f;
    const double referenceTau    = ReferenceOpticalDepth( ray, bottomOfCloudKm );
    EXPECT_NEAR( CloudShadowTransmittance( encoded, bottomOfCloudKm, 1.0f ),
                 static_cast<float>( std::exp( -referenceTau ) ), 5e-3f );

    // The front depth is the CLOUD's front and not the shell's, which is the other half of the same
    // statement: a shell entered 0.8 km above the cloud must not shade the 0.8 km of clear air under it.
    EXPECT_GT( data.FrontDepthKm, ray.EnterKm + 0.5f );
    EXPECT_FLOAT_EQ( CloudShadowTransmittance( encoded, ray.EnterKm + 0.2f, 1.0f ), 1.0f );
}

// Monotonicity. More of the ray behind you is more material in the way, always — an inversion here would
// mean a receiver deeper in the cloud lit MORE brightly than one at its edge, which reads as a normal
// problem rather than as a shadow problem.
TEST( CloudShadowMap, OpticalDepthNeverDecreasesWithDepth )
{
    SetUniformSlab( 8.0f );

    const TexelRay         ray  = MakeTexelRay( 60.0f, 0.2f );
    const float            far  = CloudShadowFarDepthKm( CLOUD_SHADOWMAP_EXTENT_KM );
    const CloudShadowTexel data = CloudShadowTraceRay( ray.OriginKm, ray.Direction, ray.EnterKm, ray.ExitKm,
                                                       static_cast<int>( CLOUD_SHADOWMAP_BASE_SAMPLES ), far );

    float previous = -1.0f;
    for ( int i = 0; i <= 200; ++i )
    {
        const float depthKm = far * ( i / 200.0f );
        const float tau     = CloudShadowOpticalDepth( data, depthKm );
        EXPECT_GE( tau, previous );
        previous = tau;
    }
}

// A ray that never meets the shell must read as fully lit at EVERY depth, and it must do so by the
// arithmetic rather than by a special case — the clear texel puts the front depth at the far plane, which
// no receiver can be past.
TEST( CloudShadowMap, AMissedShellIsFullyLitEverywhere )
{
    SetUniformSlab( 8.0f );

    const float far = CloudShadowFarDepthKm( CLOUD_SHADOWMAP_EXTENT_KM );
    // tExit <= tEnter is CloudLayerIntersect's own encoding of "nothing to march".
    const CloudShadowTexel data    = CloudShadowTraceRay( vec3( 0.0f, kPlanetKm + 60.0f, 0.0f ),
                                                          vec3( 0.0f, -1.0f, 0.0f ), 1.0f, -1.0f, 32, far );
    const vec3             encoded = CloudShadowEncode( data );

    for ( int i = 0; i <= 20; ++i )
        EXPECT_FLOAT_EQ( CloudShadowTransmittance( encoded, far * ( i / 20.0f ), 1.0f ), 1.0f );
}

// The strength is applied at READ time rather than baked into the map, which is where this parts company
// with Unreal. That is only legitimate if it commutes with the clamp — and it does, because both
// arguments of the min() scale together. Asserted rather than argued.
TEST( CloudShadowMap, StrengthScalesTheOpticalDepthAndCommutesWithTheClamp )
{
    SetUniformSlab( 8.0f );

    const TexelRay         ray  = MakeTexelRay( 60.0f, 0.0f );
    const float            far  = CloudShadowFarDepthKm( CLOUD_SHADOWMAP_EXTENT_KM );
    const CloudShadowTexel data = CloudShadowTraceRay( ray.OriginKm, ray.Direction, ray.EnterKm, ray.ExitKm,
                                                       static_cast<int>( CLOUD_SHADOWMAP_BASE_SAMPLES ), far );

    // The same texel with both channels pre-scaled — Unreal's arrangement — must give the same answer as
    // scaling the reconstruction.
    CloudShadowTexel baked    = data;
    const float      strength = 0.35f;
    baked.MeanExtinctionPerKm *= strength;
    baked.MaxOpticalDepth *= strength;

    const vec3 encodedRaw   = CloudShadowEncode( data );
    const vec3 encodedBaked = CloudShadowEncode( baked );

    for ( int i = 0; i <= 40; ++i )
    {
        const float depthKm = far * ( i / 40.0f );
        EXPECT_NEAR( CloudShadowTransmittance( encodedRaw, depthKm, strength ),
                     CloudShadowTransmittance( encodedBaked, depthKm, 1.0f ), 1e-6f );
    }

    // Zero strength is fully lit everywhere, which is what makes the component's Shadow Strength dial
    // reach "off" exactly rather than nearly.
    EXPECT_FLOAT_EQ( CloudShadowTransmittance( encodedRaw, far, 0.0f ), 1.0f );
}

// ---------------------------------------------------------------------------------------------------
// 2. THE C++ MIRROR AND THE SHADER'S OWN CONSTANTS
// ---------------------------------------------------------------------------------------------------

TEST( CloudShadowMap, TheCppMirrorAgreesWithTheShaderConstants )
{
    EXPECT_FLOAT_EQ( static_cast<float>( Desert::Graphic::kCloudShadowMapResolution ),
                     CLOUD_SHADOWMAP_RESOLUTION );
    EXPECT_FLOAT_EQ( Desert::Graphic::kCloudShadowExtentKm, CLOUD_SHADOWMAP_EXTENT_KM );
    EXPECT_FLOAT_EQ( Desert::Graphic::kCloudShadowSnapKm, CLOUD_SHADOWMAP_SNAP_KM );
    EXPECT_FLOAT_EQ( Desert::Graphic::kCloudShadowBorderFadeUv, CLOUD_SHADOWMAP_BORDER_FADE_UV );
    EXPECT_FLOAT_EQ( Desert::Graphic::kCloudShadowBaseSamples, CLOUD_SHADOWMAP_BASE_SAMPLES );
    EXPECT_FLOAT_EQ( Desert::Graphic::kCloudShadowHorizonMultiplier, CLOUD_SHADOWMAP_HORIZON_MULTIPLIER );
}

TEST( CloudShadowMap, TheCppMirrorAgreesWithTheShaderFunctions )
{
    for ( int i = -400; i <= 400; ++i )
    {
        const float value = i * 137.0f;
        EXPECT_FLOAT_EQ( Desert::Graphic::CloudShadowSnapToGrid( value, 2000.0f ),
                         CloudShadowSnapToGrid( value, 2000.0f ) );
    }

    for ( int i = -256; i <= 256; ++i )
    {
        const float clipComponent = i / 200.0f;
        EXPECT_FLOAT_EQ( Desert::Graphic::CloudShadowSnapToPixelGrid( clipComponent, CLOUD_SHADOWMAP_RESOLUTION ),
                         CloudShadowSnapToPixelGrid( clipComponent, CLOUD_SHADOWMAP_RESOLUTION ) );
    }

    for ( int i = 0; i <= 90; ++i )
    {
        const float sunUpDot = std::sin( glm::radians( static_cast<float>( i ) ) );
        EXPECT_FLOAT_EQ( Desert::Graphic::CloudShadowSampleCount( sunUpDot, CLOUD_SHADOWMAP_BASE_SAMPLES,
                                                                  CLOUD_SHADOWMAP_HORIZON_MULTIPLIER ),
                         CloudShadowSampleCount( sunUpDot ) );
    }
}

// The sample count is a COST as well as a quality, so both ends of it are pinned rather than trusted.
//
// AND THE HIGH END IS NOT THE BASE COUNT, which is worth an assertion of its own because it is
// surprising: Unreal's horizon factor is `clamp(0.2 / sin(elevation), 0, 1)`, which is 0.2 and not 0 with
// the sun straight overhead. The count therefore never falls below 1.2x the base — 38.4 samples where a
// reading of the constant alone would predict 32. That is twenty per cent of this pass's whole cost, and
// it is the difference between the price list in CALIBRATION.md being right and being a fifth optimistic.
TEST( CloudShadowMap, TheSampleCountStaysBetweenItsTwoEnds )
{
    for ( int i = 0; i <= 90; ++i )
    {
        const float count = CloudShadowSampleCount( std::sin( glm::radians( static_cast<float>( i ) ) ) );
        EXPECT_GE( count, CLOUD_SHADOWMAP_BASE_SAMPLES );
        EXPECT_LE( count, CLOUD_SHADOWMAP_BASE_SAMPLES * CLOUD_SHADOWMAP_HORIZON_MULTIPLIER );
    }

    // Sun overhead: the floor of the formula, 1.2x the base.
    EXPECT_FLOAT_EQ( CloudShadowSampleCount( 1.0f ), CLOUD_SHADOWMAP_BASE_SAMPLES * 1.2f );
    // Sun at or below 11.5 degrees of elevation, where the factor saturates: the full multiplier.
    EXPECT_FLOAT_EQ( CloudShadowSampleCount( 0.0f ),
                     CLOUD_SHADOWMAP_BASE_SAMPLES * CLOUD_SHADOWMAP_HORIZON_MULTIPLIER );
    EXPECT_FLOAT_EQ( CloudShadowSampleCount( 0.2f ),
                     CLOUD_SHADOWMAP_BASE_SAMPLES * CLOUD_SHADOWMAP_HORIZON_MULTIPLIER );
    // And the count rises as the sun falls, everywhere in between.
    float previous = 0.0f;
    for ( int i = 90; i >= 0; --i )
    {
        const float count = CloudShadowSampleCount( std::sin( glm::radians( static_cast<float>( i ) ) ) );
        EXPECT_GE( count, previous );
        previous = count;
    }
}

// ---------------------------------------------------------------------------------------------------
// 3. THE MAP'S TEXEL AND THE MARCH'S OWN RESOLUTION
// ---------------------------------------------------------------------------------------------------

// The extent was derived FROM the resolution and the finest chord the view march can find. This is that
// derivation, asserted: a texel coarser than the chord throws away detail the sky visibly has, and one
// much finer stores detail the producer cannot make. If somebody moves the step schedule, the component's
// default Max Steps or the extent, this is the line that says the three no longer add up.
TEST( CloudShadowMap, TheTexelResolvesWhatTheMarchCanFind )
{
    // The component's default ceiling. Stated here rather than read from the component for the reason the
    // layer numbers above are: this suite owns the relation, ComponentReflection owns the default.
    constexpr float kDefaultMaxSteps = 256.0f;

    const float texelKm = CloudShadowTexelKm( CLOUD_SHADOWMAP_EXTENT_KM, CLOUD_SHADOWMAP_RESOLUTION );
    const float chordKm = CloudFinestResolvableChordKm( kDefaultMaxSteps );

    EXPECT_LE( texelKm, chordKm ) << "the shadow map is coarser than the finest cloud the march can find";
    EXPECT_GE( texelKm, chordKm * 0.5f )
         << "the shadow map is storing detail the march cannot produce — resolution being spent on nothing";

    // And the vertical axis of the same statement: the sample spacing across the shipped congestus
    // envelope must resolve the same chord, or the map's two axes disagree about what a cloud is.
    const float envelopeKm      = kTopKm - kBaseKm;
    const float sampleSpacingKm = envelopeKm / CLOUD_SHADOWMAP_BASE_SAMPLES;
    EXPECT_LE( sampleSpacingKm, chordKm );
}

// ---------------------------------------------------------------------------------------------------
// 4. THE SNAP AND THE FRAME BEFORE IT — the whole anti-shimmer property, which no frame can show
// ---------------------------------------------------------------------------------------------------

namespace
{
    using Desert::Graphic::CloudBuildShadowMapView;
    using Desert::Graphic::CloudShadowMapView;
    using Desert::Graphic::kCloudShadowExtentKm;
    using Desert::Graphic::kCloudShadowMapResolution;
    using Desert::Graphic::kCloudShadowSnapKm;
    using Desert::Graphic::kCloudWorldUnitsPerKm;

    CloudShadowMapView BuildView( const glm::vec3& cameraWorld, const glm::vec3& toSun )
    {
        return CloudBuildShadowMapView( cameraWorld, toSun, kPlanetKm, kCloudShadowExtentKm, kCloudShadowSnapKm,
                                        static_cast<float>( kCloudShadowMapResolution ) );
    }

    const glm::vec3 kSun = glm::normalize( glm::vec3( 0.30f, 0.75f, 0.55f ) );
} // namespace

// THE ASSERTION THE WHOLE SNAP EXISTS FOR. The camera walks four kilometres and the projection does not
// move by one bit, so every piece of world stays in the texel it was in and the shadow is nailed to the
// ground. Without the snap this test fails on the second sample, and the rendered symptom is a shadow
// that boils in place under a moving camera — which is invisible in a still frame BY CONSTRUCTION,
// because a still frame has exactly one projection.
TEST( CloudShadowMap, TheProjectionIsIdenticalWhileTheCameraStaysInsideOneSnapCell )
{
    const CloudShadowMapView reference = BuildView( glm::vec3( 0.0f, 150000.0f, 0.0f ), kSun );

    for ( int i = 0; i <= 40; ++i )
    {
        // Four kilometres of travel in 100 m steps, plus a climb — well inside the 20 km grid.
        const glm::vec3          camera( i * 10000.0f, 150000.0f + i * 2000.0f, i * 7000.0f );
        const CloudShadowMapView view = BuildView( camera, kSun );

        for ( int c = 0; c < 4; ++c )
            for ( int r = 0; r < 4; ++r )
                EXPECT_FLOAT_EQ( view.WorldToMap[c][r], reference.WorldToMap[c][r] )
                     << "camera step " << i << ", matrix element (" << c << ", " << r << ")";
    }
}

// The other half of the same property: when the camera DOES leave the cell the map moves by exactly one
// grid step, not by an arbitrary amount. A snap that drifted would be a snap that only postpones the
// shimmer.
TEST( CloudShadowMap, LeavingTheCellMovesTheMapByExactlyOneGridStep )
{
    const float snapWorld = kCloudShadowSnapKm * kCloudWorldUnitsPerKm;

    // Two cameras exactly one grid step apart. Stated that way rather than as "either side of a cell
    // boundary" because the boundary is not where a first reading puts it: the map is centred on the
    // point of the planet's SURFACE under the camera, which sits inside the camera's own radius by the
    // curvature, so a camera at half a step is still in the first cell.
    const CloudShadowMapView a = BuildView( glm::vec3( 0.0f, 150000.0f, 0.0f ), kSun );
    const CloudShadowMapView b = BuildView( glm::vec3( snapWorld, 150000.0f, 0.0f ), kSun );

    // The world point that sits at the centre of each map — the map's own anchor — differs by one step.
    const glm::vec4 centreA = a.MapToWorld * glm::vec4( 0.0f, 0.0f, 0.5f, 1.0f );
    const glm::vec4 centreB = b.MapToWorld * glm::vec4( 0.0f, 0.0f, 0.5f, 1.0f );

    // ONE GRID STEP, to within ONE TEXEL — and the residual is not slop, it is the second snap. The pixel
    // grid snap that follows moves the finished projection by up to half a texel in each axis so the
    // world origin lands on the lattice, so the two centres cannot be exactly a grid step apart and must
    // not be: that half texel is the whole point of the second snap.
    const float texelWorld =
         CloudShadowTexelKm( kCloudShadowExtentKm, static_cast<float>( kCloudShadowMapResolution ) ) *
         kCloudWorldUnitsPerKm;

    EXPECT_NEAR( centreB.x - centreA.x, snapWorld, texelWorld );
    EXPECT_NEAR( centreB.z - centreA.z, 0.0f, texelWorld );
}

// The pixel-grid snap, stated as what it achieves rather than as what it does: a FIXED world point — the
// origin — lands on the map's lattice exactly. It is the residual the coarse snap cannot remove, because
// the coarse snap freezes where the map is and not which way it points.
TEST( CloudShadowMap, TheWorldOriginLandsOnThePixelLattice )
{
    for ( int i = 0; i <= 24; ++i )
    {
        // A sun sweeping from near the horizon to overhead, which is what rotates the basis.
        const float     elevation = glm::radians( 4.0f + i * 3.5f );
        const glm::vec3 toSun( std::cos( elevation ) * 0.8f, std::sin( elevation ), std::cos( elevation ) * 0.6f );

        const CloudShadowMapView view =
             BuildView( glm::vec3( 12345.0f, 150000.0f, -6789.0f ), glm::normalize( toSun ) );

        const glm::vec4 originClip = view.WorldToMap * glm::vec4( 0.0f, 0.0f, 0.0f, 1.0f );
        const float     half       = static_cast<float>( kCloudShadowMapResolution ) * 0.5f;

        for ( int axis = 0; axis < 2; ++axis )
        {
            const float lattice = originClip[axis] * half;
            EXPECT_NEAR( lattice, std::floor( lattice + 0.5f ), 1e-3f )
                 << "sun elevation " << glm::degrees( elevation ) << " deg, axis " << axis;
        }
    }
}

// ---------------------------------------------------------------------------------------------------
// The map's bounds and its depth axis — the arithmetic the consumer performs on every lit pixel
// ---------------------------------------------------------------------------------------------------

TEST( CloudShadowMap, TheProjectionCoversExactlyTheExtentAndInvertsExactly )
{
    const CloudShadowMapView view = BuildView( glm::vec3( 0.0f, 20000.0f, 0.0f ), kSun );

    // The anchor sits at the centre of the map, at the light distance from the near plane — which is half
    // the far plane, so exactly the middle of the depth axis. That number is the one the consumer scales
    // by, and it is the one that would be wrong if the projection's near/far were ever swapped.
    const glm::vec4 anchorClip = view.WorldToMap * ( view.MapToWorld * glm::vec4( 0.0f, 0.0f, 0.5f, 1.0f ) );
    EXPECT_NEAR( anchorClip.x, 0.0f, 1e-4f );
    EXPECT_NEAR( anchorClip.y, 0.0f, 1e-4f );
    EXPECT_NEAR( anchorClip.z, 0.5f, 1e-4f );

    EXPECT_NEAR( view.FarDepthKm, 4.0f * kCloudShadowExtentKm, 1e-3f );
    EXPECT_NEAR( CloudShadowDepthKm( 0.5f, view.FarDepthKm ), 2.0f * kCloudShadowExtentKm, 1e-3f );

    // A round trip through both matrices returns the point it started at, to well inside a texel.
    const glm::vec3 probe( 123456.0f, -1000.0f, -654321.0f );
    const glm::vec4 clip = view.WorldToMap * glm::vec4( probe, 1.0f );
    const glm::vec4 back = view.MapToWorld * clip;
    EXPECT_NEAR( back.x / back.w, probe.x, 10.0f );
    EXPECT_NEAR( back.y / back.w, probe.y, 10.0f );
    EXPECT_NEAR( back.z / back.w, probe.z, 10.0f );
}

// The border, and the fade that keeps it from being a straight line of light across a terrain.
TEST( CloudShadowMap, TheBorderFadeReachesBothEndsAndOnlyAtTheBorder )
{
    EXPECT_FLOAT_EQ( CloudShadowBorderFade( vec2( 0.5f, 0.5f ), CLOUD_SHADOWMAP_BORDER_FADE_UV ), 1.0f );
    EXPECT_FLOAT_EQ( CloudShadowBorderFade( vec2( 0.0f, 0.5f ), CLOUD_SHADOWMAP_BORDER_FADE_UV ), 0.0f );
    EXPECT_FLOAT_EQ( CloudShadowBorderFade( vec2( 1.0f, 0.5f ), CLOUD_SHADOWMAP_BORDER_FADE_UV ), 0.0f );
    EXPECT_FLOAT_EQ( CloudShadowBorderFade( vec2( 0.5f, -0.3f ), CLOUD_SHADOWMAP_BORDER_FADE_UV ), 0.0f );

    // Monotone across the band, so the fade is a gradient rather than a second edge.
    float previous = -1.0f;
    for ( int i = 0; i <= 40; ++i )
    {
        const float u    = CLOUD_SHADOWMAP_BORDER_FADE_UV * ( i / 40.0f );
        const float fade = CloudShadowBorderFade( vec2( u, 0.5f ), CLOUD_SHADOWMAP_BORDER_FADE_UV );
        EXPECT_GE( fade, previous );
        previous = fade;
    }

    // The band is a real width on the ground rather than a rounding error: two per cent of the covered
    // square is over a kilometre, which is ten texels.
    const float bandKm = 2.0f * CLOUD_SHADOWMAP_EXTENT_KM * CLOUD_SHADOWMAP_BORDER_FADE_UV;
    EXPECT_GT( bandKm, 10.0f * CloudShadowTexelKm( CLOUD_SHADOWMAP_EXTENT_KM, CLOUD_SHADOWMAP_RESOLUTION ) );
}

// The map's clip space and the UV the producer stores through are one mapping, written once. A texel
// centre must round-trip to its own index — half a texel of disagreement between the store and the fetch
// is a shadow that does not sit under its cloud.
TEST( CloudShadowMap, ClipAndUvAreOneMapping )
{
    const int resolution = static_cast<int>( CLOUD_SHADOWMAP_RESOLUTION );
    for ( int i : { 0, 1, 37, resolution / 2, resolution - 2, resolution - 1 } )
    {
        const float uvIn  = ( static_cast<float>( i ) + 0.5f ) / static_cast<float>( resolution );
        const float clip  = uvIn * 2.0f - 1.0f;
        const vec2  uvOut = CloudShadowClipToUv( vec2( clip, clip ) );

        EXPECT_NEAR( uvOut.x, uvIn, 1e-6f );
        EXPECT_EQ( static_cast<int>( uvOut.x * static_cast<float>( resolution ) ), i );
    }
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
