#include "CloudNoiseReference.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

using namespace Desert::Tests::CloudNoiseRef;

namespace
{
    // The volume the bake writes is 128^3 and its coarsest channel has a period of four lattice cells, so
    // a sample grid of 64 per axis is two samples per voxel on the coarse channel — enough to catch a
    // seam, cheap enough to run in a unit test.
    constexpr int   kGrid   = 64;
    constexpr float kPeriod = 4.0f;

    float SampleUnitCube( int x, int y, int z, uint seed, int octaves )
    {
        const vec3 uvw{ ( static_cast<float>( x ) + 0.5f ) / kGrid, ( static_cast<float>( y ) + 0.5f ) / kGrid,
                        ( static_cast<float>( z ) + 0.5f ) / kGrid };
        return CloudPerlinFbm01( uvw * kPeriod, kPeriod, seed, octaves );
    }
} // namespace

// ---------------------------------------------------------------------------------------------------
// Periodicity. This is the property the whole tiling scheme rests on: the volume is sampled with REPEAT
// at an arbitrary world scale, so a field that only ALMOST tiles puts a seam across the sky at every
// multiple of the tile — a defect that is invisible at the origin and unmistakable from a moving camera.
// ---------------------------------------------------------------------------------------------------

TEST( CloudNoisePeriodicity, TheFieldRepeatsExactlyAfterOnePeriodOnEveryAxis )
{
    constexpr uint  kSeed    = 1337u;
    constexpr float kSamples = 17.0f;

    for ( float i = 0.0f; i < kSamples; i += 1.0f )
    {
        for ( float j = 0.0f; j < kSamples; j += 1.0f )
        {
            const vec3 p{ i * 0.37f, j * 0.53f, ( i + j ) * 0.29f };

            const float base = CloudPerlinPeriodic( p, kPeriod, kSeed );

            // NEAR rather than exact equality, and the tolerance is float arithmetic rather than slack in
            // the tiling. The lattice index is exact on both sides — that is what the wrap guarantees —
            // but the FRACTIONAL part is computed as `x - floor(x)`, and at x = 9.92 that subtraction
            // discards different low bits than it does at x = 5.92. The error is a few ULP of the
            // interpolant, six orders below anything the erosion threshold can act on.
            constexpr float kTolerance = 1e-5f;
            EXPECT_NEAR( base, CloudPerlinPeriodic( p + vec3( kPeriod, 0.0f, 0.0f ), kPeriod, kSeed ),
                         kTolerance );
            EXPECT_NEAR( base, CloudPerlinPeriodic( p + vec3( 0.0f, kPeriod, 0.0f ), kPeriod, kSeed ),
                         kTolerance );
            EXPECT_NEAR( base, CloudPerlinPeriodic( p + vec3( 0.0f, 0.0f, kPeriod ), kPeriod, kSeed ),
                         kTolerance );
        }
    }
}

TEST( CloudNoisePeriodicity, TheFieldRepeatsAcrossNEGATIVECoordinatesToo )
{
    // The wind offset accumulates without bound and is SUBTRACTED from the sample position, so a scene
    // left running walks into negative coordinates and stays there. `mod` rather than a cast is what makes
    // this hold; a truncating cast folds -0.5 and +0.5 onto the same cell and mirrors the field about the
    // origin, which reads as the sky briefly running backwards.
    constexpr uint kSeed = 7u;

    for ( float i = 1.0f; i < 12.0f; i += 1.0f )
    {
        const vec3 p{ -i * 1.7f, -i * 0.9f, -i * 2.3f };
        EXPECT_NEAR( CloudPerlinPeriodic( p, kPeriod, kSeed ),
                     CloudPerlinPeriodic( p + vec3( kPeriod * 3.0f ), kPeriod, kSeed ), 1e-5f );
    }
}

TEST( CloudNoisePeriodicity, TheFractalSumTilesAtTheBASEPeriodDespiteItsOctavesDoublingTheFrequency )
{
    // The octave's period doubles with its frequency for exactly this reason. Had it not, the second
    // octave would wrap halfway through its own cells and the sum would tile at twice the base period —
    // or, with three octaves, not at all.
    constexpr uint kSeed = 99u;

    for ( int octaves = 1; octaves <= 5; ++octaves )
    {
        for ( float i = 0.0f; i < 9.0f; i += 1.0f )
        {
            const vec3 p{ i * 0.61f, i * 0.13f, i * 0.87f };
            EXPECT_NEAR( CloudPerlinFbm( p, kPeriod, kSeed, octaves ),
                         CloudPerlinFbm( p + vec3( kPeriod, kPeriod, kPeriod ), kPeriod, kSeed, octaves ), 1e-5f )
                 << "octaves = " << octaves;
        }
    }
}

// ---------------------------------------------------------------------------------------------------
// Lattice behaviour
// ---------------------------------------------------------------------------------------------------

TEST( CloudNoiseLattice, GradientNoiseIsZeroAtEveryLatticePoint )
{
    // A defining property of gradient noise, and the cheapest possible check that the fade, the gradient
    // selector and the interpolation are wired to the same corners: at an integer coordinate every
    // gradient is dotted with a zero offset.
    for ( float x = -3.0f; x <= 3.0f; x += 1.0f )
        for ( float y = -3.0f; y <= 3.0f; y += 1.0f )
            for ( float z = -3.0f; z <= 3.0f; z += 1.0f )
                EXPECT_NEAR( CloudPerlinPeriodic( vec3( x, y, z ), kPeriod, 5u ), 0.0f, 1e-6f );
}

TEST( CloudNoiseLattice, TheFadeCurveIsFlatAtBothEnds )
{
    // 6t^5-15t^4+10t^3 has a vanishing first AND second derivative at 0 and 1. The second derivative is
    // what the cubic alternative gets wrong, and the symptom is a faint rectangular grid appearing once
    // the noise is used as an erosion threshold rather than as a colour.
    EXPECT_FLOAT_EQ( CloudFade( 0.0f ), 0.0f );
    EXPECT_FLOAT_EQ( CloudFade( 1.0f ), 1.0f );
    EXPECT_NEAR( CloudFade( 0.5f ), 0.5f, 1e-6f );

    constexpr float kEps        = 1e-3f;
    const float     slopeAtZero = ( CloudFade( kEps ) - CloudFade( 0.0f ) ) / kEps;
    const float     slopeAtOne  = ( CloudFade( 1.0f ) - CloudFade( 1.0f - kEps ) ) / kEps;
    EXPECT_LT( slopeAtZero, 1e-4f );
    EXPECT_LT( slopeAtOne, 1e-4f );
}

TEST( CloudNoiseHash, OneChangedInputBitChangesAboutHalfTheOutputBits )
{
    // The avalanche property, measured rather than asserted. Without it, neighbouring lattice cells stay
    // correlated and the noise shows visible structure along the axes.
    int    trials     = 0;
    double totalRatio = 0.0;

    for ( uint x = 0u; x < 32u; ++x )
    {
        for ( uint bit = 0u; bit < 32u; ++bit )
        {
            const uint a = CloudHashCell( x, 11u, 23u, 4242u );
            const uint b = CloudHashCell( x ^ ( 1u << bit ), 11u, 23u, 4242u );

            uint diff    = a ^ b;
            int  changed = 0;
            for ( uint i = 0u; i < 32u; ++i )
                changed += static_cast<int>( ( diff >> i ) & 1u );

            totalRatio += static_cast<double>( changed ) / 32.0;
            ++trials;
        }
    }

    const double mean = totalRatio / trials;
    EXPECT_GT( mean, 0.40 ) << "hash does not avalanche: " << mean;
    EXPECT_LT( mean, 0.60 ) << "hash does not avalanche: " << mean;
}

TEST( CloudNoiseHash, DifferentSeedsProduceDifferentFields )
{
    int differing = 0;
    for ( int i = 0; i < kGrid; ++i )
    {
        const float a = SampleUnitCube( i, i, i, 1u, 3 );
        const float b = SampleUnitCube( i, i, i, 2u, 3 );
        if ( std::abs( a - b ) > 1e-4f )
            ++differing;
    }
    EXPECT_GT( differing, kGrid / 2 );
}

// ---------------------------------------------------------------------------------------------------
// RANGE. This is the test that exists because a number was guessed and the frame disagreed.
//
// The coverage threshold is `remap(field, 1 - Coverage, 1, 0, 1)`, which silently assumes the field
// REACHES 1. Perlin's practical range is much narrower than its theoretical one, and normalizing a
// fractal sum of it by the sum of the amplitudes narrows it further — so if the field tops out near 0.75,
// the coverage can never exceed 0.75 either, every cloud is thin, and the only visible symptom is that
// the sky looks like haze at every setting of a slider that appears to work.
// ---------------------------------------------------------------------------------------------------

TEST( CloudNoiseRange, TheUnitFieldSpansEnoughOfZeroToOneForTheCoverageThresholdToMeanSomething )
{
    std::vector<float> samples;
    samples.reserve( static_cast<size_t>( kGrid ) * kGrid * kGrid );

    for ( int z = 0; z < kGrid; ++z )
        for ( int y = 0; y < kGrid; ++y )
            for ( int x = 0; x < kGrid; ++x )
                samples.push_back( SampleUnitCube( x, y, z, 1337u, 3 ) );

    std::sort( samples.begin(), samples.end() );

    const float minimum = samples.front();
    const float maximum = samples.back();
    const float p01     = samples[samples.size() / 100];
    const float p99     = samples[samples.size() * 99 / 100];

    // Reported so that the numbers behind the gain are on the record rather than in somebody's memory.
    std::printf( "[CloudNoise] unit field: min %.4f  p01 %.4f  p99 %.4f  max %.4f\n", minimum, p01, p99, maximum );

    EXPECT_GE( minimum, 0.0f );
    EXPECT_LE( maximum, 1.0f );

    // The load-bearing assertion: the top of the field has to come close enough to 1 that a coverage of
    // 1.0 can actually saturate it. Below this the Coverage slider reaches its maximum and the sky is
    // still translucent.
    EXPECT_GT( p99, 0.90f ) << "the field never approaches 1, so full Coverage cannot produce solid cloud";
    EXPECT_LT( p01, 0.10f ) << "the field never approaches 0, so low Coverage cannot produce clear sky";
}

TEST( CloudNoiseRange, TheFieldReachesBothEndsAtEveryOctaveCountTheComponentAllows )
{
    // The gain is one number and the octave count is an artist slider, so the property has to hold across
    // the whole range the component clamps to — otherwise raising the octaves would quietly stop full
    // Coverage from producing solid cloud, and the only symptom would be that the sky got thinner.
    for ( int octaves = 1; octaves <= 6; ++octaves )
    {
        std::vector<float> samples;
        samples.reserve( 32 * 32 * 32 );
        for ( int z = 0; z < 32; ++z )
            for ( int y = 0; y < 32; ++y )
                for ( int x = 0; x < 32; ++x )
                {
                    const vec3 uvw{ ( x + 0.5f ) / 32.0f, ( y + 0.5f ) / 32.0f, ( z + 0.5f ) / 32.0f };
                    samples.push_back( CloudPerlinFbm01( uvw * kPeriod, kPeriod, 4242u, octaves ) );
                }

        std::sort( samples.begin(), samples.end() );
        const float p01 = samples[samples.size() / 100];
        const float p99 = samples[samples.size() * 99 / 100];

        EXPECT_GT( p99, 0.90f ) << "octaves = " << octaves;
        EXPECT_LT( p01, 0.10f ) << "octaves = " << octaves;
    }
}

TEST( CloudNoiseRange, TheRemapIsSafeWhenItsSourceRangeIsDegenerate )
{
    // One hand-edited scene file away, and the unguarded form is a division by zero that turns the whole
    // layer into NaN — which renders as a black sky, not as a warning.
    EXPECT_TRUE( std::isfinite( CloudRemap( 0.5f, 0.3f, 0.3f, 0.0f, 1.0f ) ) );
    EXPECT_FLOAT_EQ( CloudRemap( 0.0f, 0.0f, 1.0f, 0.0f, 1.0f ), 0.0f );
    EXPECT_FLOAT_EQ( CloudRemap( 1.0f, 0.0f, 1.0f, 0.0f, 1.0f ), 1.0f );
    EXPECT_FLOAT_EQ( CloudRemap( -5.0f, 0.0f, 1.0f, 0.0f, 1.0f ), 0.0f ) << "clamped below";
    EXPECT_FLOAT_EQ( CloudRemap( 5.0f, 0.0f, 1.0f, 0.0f, 1.0f ), 1.0f ) << "clamped above";
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
