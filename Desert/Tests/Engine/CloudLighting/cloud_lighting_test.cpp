// The scattering maths of the cloud march, tested against the physics it claims to implement.
//
// Common/CloudLighting.glslh is compiled here AS C++ (CloudLightingReference.hpp), so every assertion is
// about the text the march compiles. Each test is a RELATION rather than a spot value, because a spot
// value of a phase function is a number nobody can check by eye:
//
//   * THE PHASE FUNCTION INTEGRATES TO ONE over the sphere. That is what makes it a redistribution of
//     light rather than a gain applied to it, and it is the property a hand-tuned normalization breaks
//     while still looking plausible in a frame.
//   * BEER'S LAW IS MULTIPLICATIVE across a split. The march multiplies transmittance step by step, so if
//     the function were not multiplicative the answer would depend on the step schedule.
//   * THE SCATTER INTEGRAL IS THE CLOSED FORM, pinned against a numeric integration of the same step —
//     the way Desert/Tests/Engine/HeightFog pins its own closed form against a Riemann sum — and, the
//     property that motivates it at all, INVARIANT to how the distance is subdivided. A naive `S * dt`
//     passes every check that looks at one step and fails this one, and the symptom would be that a cloud
//     changes brightness with distance and with the quality tier.
//   * THE AMBIENT OCCLUSION IS REMOVABLE BY ITS STRENGTH and monotone in the profile.

#include "CloudLightingReference.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>

using namespace Desert::Tests::CloudLightingRef;

namespace
{
    // 2*pi * integral over mu in [-1, 1], by the midpoint rule. The phase depends on the direction only
    // through cos(theta), so the azimuth contributes its 2*pi analytically and the quadrature is
    // one-dimensional. Half a million samples puts the rule's error orders below the tolerances used
    // against it, even at the sharpest g tested here.
    double IntegratePhaseOverTheSphere( float g, int samples )
    {
        const double dmu = 2.0 / samples;

        double sum = 0.0;
        for ( int i = 0; i < samples; ++i )
        {
            const double mu = -1.0 + ( static_cast<double>( i ) + 0.5 ) * dmu;
            sum += static_cast<double>( CloudPhaseHG( static_cast<float>( mu ), g ) ) * dmu;
        }

        return sum * 2.0 * static_cast<double>( CLOUD_PI );
    }

    // The integral the closed form claims to be: in-scattered radiance S attenuated by the medium it is
    // travelling through, across one step. Midpoint rule, 4096 steps.
    double NumericScatterIntegral( double scattering, double extinctionPerKm, double stepKm )
    {
        constexpr int n = 4096;

        const double h   = stepKm / n;
        double       sum = 0.0;
        for ( int i = 0; i < n; ++i )
        {
            const double s = ( static_cast<double>( i ) + 0.5 ) * h;
            sum += scattering * std::exp( -extinctionPerKm * s ) * h;
        }
        return sum;
    }

    // The relative tolerance a float32 evaluation of (S - S*T)/sigma deserves at a given optical depth.
    //
    // The subtraction is a CANCELLATION: at sigma*dt = 5e-5 the two operands agree to four digits, so the
    // difference keeps only what is left of float32's seven — roughly eps/(sigma*dt) of relative error,
    // which is 0.2 per cent there and negligible by sigma*dt = 0.1. Writing the tolerance as the formula
    // rather than as one loose number keeps the test tight where the arithmetic is accurate, which is
    // where a wrong closed form would hide.
    double ScatterTolerance( double opticalDepth )
    {
        return 1e-4 + 3e-7 / std::max( opticalDepth, 1e-9 );
    }
} // namespace

// ---------------------------------------------------------------------------------------------------
// CloudPhaseHG
// ---------------------------------------------------------------------------------------------------

TEST( CloudLightingPhase, ItIntegratesToOneOverTheSphereAtEveryAsymmetryTheComponentAllows )
{
    // THE DEFINING PROPERTY, and the one that says the function redistributes light rather than creating
    // it. The component's Phase G range is -0.9 to 0.9; the sampled set spans it.
    for ( const float g : { -0.8f, -0.6f, -0.3f, 0.0f, 0.3f, 0.6f, 0.8f } )
    {
        const double integral = IntegratePhaseOverTheSphere( g, 500000 );
        EXPECT_NEAR( integral, 1.0, 2e-3 ) << "g = " << g << " integrates to " << integral;
    }
}

TEST( CloudLightingPhase, AtZeroAsymmetryItIsExactlyTheIsotropicPhaseTheOctavesBlendToward )
{
    // The two are used together — the multiple-scattering octaves mix from this phase toward
    // CLOUD_ISOTROPIC_PHASE — so they have to agree at g = 0 or the first octave would step.
    for ( const float cosTheta : { -1.0f, -0.5f, 0.0f, 0.37f, 1.0f } )
        EXPECT_NEAR( CloudPhaseHG( cosTheta, 0.0f ), CLOUD_ISOTROPIC_PHASE, 1e-8f ) << "cos = " << cosTheta;
}

TEST( CloudLightingPhase, PositiveAsymmetryScattersFORWARDAndNegativeBackward )
{
    // The sign convention, which is what puts the bright rim on the cloud you look at through the sun.
    // Asserted as monotonicity across the whole range rather than at the two ends, because a function
    // that is merely brighter at cos = 1 could still be non-monotone in between.
    for ( const float g : { 0.2f, 0.6f, 0.9f } )
    {
        float previous = -1.0f;
        for ( int step = 0; step <= 200; ++step )
        {
            const float cosTheta = -1.0f + 0.01f * static_cast<float>( step );
            const float phase    = CloudPhaseHG( cosTheta, g );
            EXPECT_GE( phase, previous ) << "g = " << g << ", cos = " << cosTheta << " went DOWN";
            previous = phase;
        }

        // The forward/backward ratio of Henyey-Greenstein is ((1+g)/(1-g))^3 exactly, which is 3.4 at
        // g = 0.2 and 64 at g = 0.6. Asserted against that closed form rather than against a round
        // number, so the test says how strongly it forward-scatters and not merely that it does.
        const float ratio = std::pow( ( 1.0f + g ) / ( 1.0f - g ), 3.0f );
        EXPECT_NEAR( CloudPhaseHG( 1.0f, g ) / CloudPhaseHG( -1.0f, g ), ratio, ratio * 1e-3f ) << "g = " << g;

        EXPECT_LT( CloudPhaseHG( 1.0f, -g ), CloudPhaseHG( -1.0f, -g ) ) << "g = " << -g;
    }
}

TEST( CloudLightingPhase, ItStaysFiniteAndPositiveAtTheOneAlignmentThatWouldDivideByZero )
{
    // g -> 1 with the view, the sun and the sample in a line drives the denominator to zero, and the
    // unguarded pow(0, 1.5) is an infinity that propagates through the integral and lands as a
    // permanently white pixel. It needs one exact alignment, so no sampled frame is evidence against it.
    for ( const float g : { 0.9f, 0.95f, 0.999f, 1.0f, 2.0f, -1.0f, -2.0f } )
    {
        for ( const float cosTheta : { -1.0f, -0.999f, 0.0f, 0.999f, 1.0f } )
        {
            const float phase = CloudPhaseHG( cosTheta, g );
            EXPECT_TRUE( std::isfinite( phase ) ) << "g = " << g << ", cos = " << cosTheta;
            EXPECT_GT( phase, 0.0f ) << "g = " << g << ", cos = " << cosTheta;
        }
    }
}

// ---------------------------------------------------------------------------------------------------
// CloudBeerTransmittance
// ---------------------------------------------------------------------------------------------------

TEST( CloudLightingBeer, OneAtZeroDistanceMonotoneDecreasingAndEqualToTheExponential )
{
    constexpr float kSigma = 8.0f; // the component's Extinction Scale default, per kilometre

    EXPECT_FLOAT_EQ( CloudBeerTransmittance( kSigma, 0.0f ), 1.0f );

    float previous = 1.0f;
    for ( int step = 0; step <= 200; ++step )
    {
        const float distanceKm = 0.01f * static_cast<float>( step );
        const float actual     = CloudBeerTransmittance( kSigma, distanceKm );

        EXPECT_NEAR( actual, std::exp( -kSigma * distanceKm ), 1e-6f ) << "d = " << distanceKm;
        EXPECT_LE( actual, previous ) << "d = " << distanceKm << " rose";
        EXPECT_GE( actual, 0.0f );
        EXPECT_LE( actual, 1.0f );
        previous = actual;
    }
}

TEST( CloudLightingBeer, ItIsMultiplicativeAcrossASplitWhichIsWhatMakesTheMarchStepwise )
{
    // The march multiplies the running transmittance by one step's worth at a time. If the function were
    // not multiplicative the total would depend on the step schedule, which is to say on the distance
    // from the camera — the same class of defect the scatter integral below is written to avoid.
    constexpr float kSigma = 8.0f;

    for ( const float total : { 0.05f, 0.4f, 1.7f } )
    {
        float product = 1.0f;
        for ( int i = 0; i < 10; ++i )
            product *= CloudBeerTransmittance( kSigma, total / 10.0f );

        EXPECT_NEAR( product, CloudBeerTransmittance( kSigma, total ), 1e-5f ) << "total = " << total;
    }
}

TEST( CloudLightingBeer, NegativeInputsAreClampedRatherThanAmplifying )
{
    // A negative distance is what a step schedule produces the moment a segment comes back inverted, and
    // exp of a positive product is a transmittance ABOVE one — light created out of nothing, which reads
    // as a blown-out band rather than as an error.
    EXPECT_FLOAT_EQ( CloudBeerTransmittance( 8.0f, -1.0f ), 1.0f );
    EXPECT_FLOAT_EQ( CloudBeerTransmittance( -8.0f, 1.0f ), 1.0f );
}

// ---------------------------------------------------------------------------------------------------
// CloudScatterIntegral — the closed form, and the reason it is not `S * dt`
// ---------------------------------------------------------------------------------------------------

TEST( CloudLightingScatter, TheClosedFormMatchesANumericIntegrationOfTheSameStep )
{
    // The pin, in the shape HeightFog uses for its own closed form: the analytic expression against a
    // brute-force integration of the medium it claims to integrate. A lost factor, a sign, or an
    // extinction applied once too often breaks this and nothing else.
    for ( const float sigma : { 0.05f, 1.0f, 8.0f, 45.0f } )
    {
        for ( const float stepKm : { 0.001f, 0.02f, 0.25f, 2.0f } )
        {
            const vec3 scattering( 0.7f, 1.3f, 2.9f );
            const vec3 actual = CloudScatterIntegral( scattering, sigma, stepKm );

            const double tolerance = ScatterTolerance( static_cast<double>( sigma ) * stepKm );

            for ( int channel = 0; channel < 3; ++channel )
            {
                const double expected = NumericScatterIntegral( scattering[channel], sigma, stepKm );
                EXPECT_NEAR( actual[channel], expected, expected * tolerance )
                     << "sigma " << sigma << ", step " << stepKm << ", channel " << channel;
            }
        }
    }
}

TEST( CloudLightingScatter, TheSameDistanceInOneStepAndInTenGivesTheSameRadiance )
{
    // THE PROPERTY THAT MOTIVATES THE CLOSED FORM. The march accumulates
    // `luminance += transmittance * CloudScatterIntegral(...)` and then attenuates the running
    // transmittance, so subdividing a step must not change the total. If it does, a cloud is brighter at
    // one quality tier than another and brighter near the camera than far from it — two tiers that
    // disagree about how bright a cloud is, which is the same defect as two shaders that disagree about
    // a constant and just as invisible in a unit test of either alone.
    for ( const float sigma : { 0.05f, 1.0f, 8.0f, 45.0f } )
    {
        for ( const float totalKm : { 0.01f, 0.2f, 1.5f } )
        {
            const vec3 scattering( 1.0f, 1.0f, 1.0f );

            const vec3 oneStep = CloudScatterIntegral( scattering, sigma, totalKm );

            vec3  tenSteps( 0.0f );
            float transmittance = 1.0f;
            for ( int i = 0; i < 10; ++i )
            {
                tenSteps += transmittance * CloudScatterIntegral( scattering, sigma, totalKm / 10.0f );
                transmittance *= CloudBeerTransmittance( sigma, totalKm / 10.0f );
            }

            const double opticalDepth = static_cast<double>( sigma ) * totalKm;
            const double tolerance    = ScatterTolerance( opticalDepth / 10.0 );

            EXPECT_NEAR( tenSteps.x, oneStep.x, oneStep.x * tolerance )
                 << "sigma " << sigma << ", total " << totalKm;

            // The naive product is what the tolerance above has to discriminate against, so the margin is
            // MEASURED rather than assumed. It only discriminates once the segment is optically thick
            // enough for the attenuation across it to matter — at an optical depth of 0.0005 the two
            // forms genuinely agree, and asserting otherwise would be asserting noise.
            if ( opticalDepth < 0.1 )
                continue;

            float naiveTransmittance = 1.0f;
            float naiveTen           = 0.0f;
            for ( int i = 0; i < 10; ++i )
            {
                naiveTen += naiveTransmittance * scattering.x * ( totalKm / 10.0f );
                naiveTransmittance *= CloudBeerTransmittance( sigma, totalKm / 10.0f );
            }
            const float naiveOne = scattering.x * totalKm;
            EXPECT_GT( std::abs( naiveOne - naiveTen ), oneStep.x * tolerance * 100.0 )
                 << "sigma " << sigma << ", total " << totalKm
                 << ": the naive form is indistinguishable here, so this configuration proves nothing";
        }
    }
}

TEST( CloudLightingScatter, TheZeroExtinctionCaseIsSAFEEvenThoughTheFloorDoesNotReachTheDocumentedLimit )
{
    // The header says the floor of 1e-6 "reproduces [the S*dt limit] to well inside half-float precision
    // while keeping the function branchless". THE FIRST HALF OF THAT IS NOT TRUE IN FLOAT32 and the
    // numbers printed below say so: with sigma floored to 1e-6, any step shorter than about a kilometre
    // makes sigma*dt smaller than float32's resolution at 1, so exp(-sigma*dt) rounds to EXACTLY one, the
    // numerator cancels to zero, and the function returns 0 rather than S*dt.
    //
    // It is harmless where it is called, which is why this is a documented measurement and not a red
    // test: at the call site `scattering` is itself `inScatter * albedo * sigma_t * scatterFactor`, so a
    // zero extinction makes the numerator zero on its own and the correct answer IS zero. What the test
    // therefore asserts is the safety envelope — finite, non-negative, never more than the unattenuated
    // product — which is what the march actually depends on.
    const vec3 scattering( 2.0f, 2.0f, 2.0f );

    for ( const float stepKm : { 0.01f, 0.5f, 3.0f } )
    {
        const vec3 actual = CloudScatterIntegral( scattering, 0.0f, stepKm );

        std::printf( "[CloudLighting] sigma 0, step %.2f km: integral %.6g, S*dt %.6g\n", stepKm, actual.x,
                     scattering.x * stepKm );

        EXPECT_TRUE( std::isfinite( actual.x ) ) << "step " << stepKm;
        EXPECT_GE( actual.x, 0.0f ) << "step " << stepKm;
        EXPECT_LE( actual.x, scattering.x * stepKm * ( 1.0f + 1e-4f ) ) << "step " << stepKm;
    }

    // Where the optical depth IS representable the limit holds, which is what says the closed form is the
    // right expression and only its floor is optimistic.
    for ( const float stepKm : { 0.05f, 0.5f } )
    {
        constexpr float kSmallSigma = 1e-3f;
        const vec3      actual      = CloudScatterIntegral( scattering, kSmallSigma, stepKm );
        EXPECT_NEAR( actual.x, scattering.x * stepKm, scattering.x * stepKm * 1e-2f ) << "step " << stepKm;
    }
}

TEST( CloudLightingScatter, ItNeverExceedsTheUnattenuatedProductAndRisesWithTheStep )
{
    // Two bounds the march relies on without stating them: one step can never in-scatter more than the
    // medium would without any absorption at all, and a longer step through the same medium can never
    // return less.
    const vec3 scattering( 1.0f, 1.0f, 1.0f );

    for ( const float sigma : { 0.5f, 8.0f, 45.0f } )
    {
        float previous = -1.0f;
        for ( int step = 0; step <= 100; ++step )
        {
            const float stepKm = 0.02f * static_cast<float>( step );
            const float value  = CloudScatterIntegral( scattering, sigma, stepKm ).x;

            EXPECT_LE( value, scattering.x * stepKm + 1e-6f ) << "sigma " << sigma << ", step " << stepKm;
            EXPECT_GE( value, previous ) << "sigma " << sigma << ", step " << stepKm << " went DOWN";
            EXPECT_TRUE( std::isfinite( value ) );
            previous = value;
        }
    }

    // A negative step is one inverted segment away and must not scatter light backwards into the frame.
    EXPECT_FLOAT_EQ( CloudScatterIntegral( scattering, 8.0f, -1.0f ).x, 0.0f );
}

// ---------------------------------------------------------------------------------------------------
// CloudAmbientOcclusion
// ---------------------------------------------------------------------------------------------------

TEST( CloudLightingAmbient, AtZeroStrengthItIsExactlyOneWhateverTheProfile )
{
    // The term has to be removable, or an artist cannot tell what it is doing. At strength 0 it must be
    // the identity for every profile, including the ends where the pow is evaluated at 0 and 1.
    for ( int step = 0; step <= 100; ++step )
    {
        const float profile = 0.01f * static_cast<float>( step );
        EXPECT_FLOAT_EQ( CloudAmbientOcclusion( profile, 0.0f ), 1.0f ) << "profile " << profile;
    }
}

TEST( CloudLightingAmbient, ItDecreasesWithDepthInsideTheBodyAndIsTheDecksSquareRoot )
{
    // Deck p.144: pow(1 - dimensional_profile, 0.5). The deeper inside the body a sample is, the less of
    // the sky's light reaches it — without this the core of a three-kilometre cumulus is lit exactly as
    // brightly as a wisp on its edge, and the cloud reads as a flat white cut-out.
    float previous = 2.0f;
    for ( int step = 0; step <= 100; ++step )
    {
        const float profile   = 0.01f * static_cast<float>( step );
        const float occlusion = CloudAmbientOcclusion( profile, 1.0f );

        EXPECT_NEAR( occlusion, std::sqrt( 1.0f - profile ), 1e-5f ) << "profile " << profile;
        EXPECT_LE( occlusion, previous ) << "profile " << profile << " rose";
        previous = occlusion;
    }

    EXPECT_FLOAT_EQ( CloudAmbientOcclusion( 0.0f, 1.0f ), 1.0f ) << "at the surface nothing is occluded";
    EXPECT_FLOAT_EQ( CloudAmbientOcclusion( 1.0f, 1.0f ), 0.0f ) << "in the core nothing gets through";
}

TEST( CloudLightingAmbient, TheStrengthBlendsBetweenNoOcclusionAndTheFullTermAndBothInputsAreClamped )
{
    for ( const float profile : { 0.1f, 0.5f, 0.9f } )
    {
        const float full = CloudAmbientOcclusion( profile, 1.0f );
        const float half = CloudAmbientOcclusion( profile, 0.5f );

        EXPECT_NEAR( half, 0.5f * ( 1.0f + full ), 1e-5f ) << "profile " << profile;
        EXPECT_GT( half, full );
        EXPECT_LT( half, 1.0f );
    }

    // Out-of-range inputs are one hand-edited scene away, and pow of a negative base is a NaN that
    // renders as a black hole in the layer rather than as an error.
    for ( const float profile : { -1.0f, 2.0f } )
        for ( const float strength : { -1.0f, 2.0f } )
            EXPECT_TRUE( std::isfinite( CloudAmbientOcclusion( profile, strength ) ) )
                 << "profile " << profile << ", strength " << strength;

    EXPECT_FLOAT_EQ( CloudAmbientOcclusion( -1.0f, 1.0f ), 1.0f );
    EXPECT_FLOAT_EQ( CloudAmbientOcclusion( 2.0f, 1.0f ), 0.0f );
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
