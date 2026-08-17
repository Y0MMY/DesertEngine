// The cloud noise, tested without a GPU.
//
// Two things are under test and they are different in kind:
//
//   1. The NOISE ITSELF — CloudNoiseReference.hpp compiles the very GLSL header the generation shaders
//      include, so the assertions below are about the code that runs on the GPU, not about a CPU
//      re-implementation of it. Four properties are asserted, and each of them is a bug we would
//      otherwise only ever see as a picture: values outside [0,1] (an unwritable texel), a seam at the
//      tile boundary (a rectangular grid drawn across the sky), non-determinism (a bug nobody else can
//      reproduce), and two seeds that produce the same cloudscape (a seed slider that does nothing).
//
//   2. The LIFECYCLE RULES — Engine/Graphic/Clouds/CloudNoiseRules.hpp, the pure decisions the ECS
//      system takes: which key a component maps to, and what to do about a scene that grows, changes or
//      loses its cloud component.

#include "CloudNoiseReference.hpp"

#include <Engine/Graphic/Clouds/CloudNoiseRules.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace Ref = Desert::Tests::CloudNoiseRef;

using Desert::Graphic::CloudCurlSeedFrom;
using Desert::Graphic::CloudNoiseAction;
using Desert::Graphic::CloudNoiseDemand;
using Desert::Graphic::CloudNoiseKey;
using Desert::Graphic::CloudNoiseLease;
using Desert::Graphic::CloudNoiseSetBytes;
using Desert::Graphic::CloudSeedFromComponent;
using Desert::Graphic::DecideCloudNoiseAction;
using Desert::Graphic::kCloudCurlNoiseSize;
using Desert::Graphic::kCloudDetailNoiseSize;
using Desert::Graphic::kCloudNoiseWorkGroupSize;
using Desert::Graphic::kCloudShapeNoiseSize;
using Desert::Graphic::MakeCloudNoiseKey;

namespace
{
    constexpr std::uint32_t kSeedA = 7u;
    constexpr std::uint32_t kSeedB = 4211u;

    // A coarse but well-spread lattice over the unit cube. Small enough to keep the suite fast (the
    // reference evaluates the same FBM the GPU does, on one core), large enough that a systematic defect
    // in any octave shows up.
    constexpr int kGridSteps = 11;

    // Texel centres of a volume of `size` texels — exactly the coordinates the shader computes.
    float TexelCentre( int index, int size )
    {
        return ( static_cast<float>( index ) + 0.5f ) / static_cast<float>( size );
    }

    bool InUnitRange( float v )
    {
        return std::isfinite( v ) && v >= 0.0f && v <= 1.0f;
    }

    void ExpectTexelInUnitRange( const glm::vec4& texel, const char* what, float x, float y, float z )
    {
        EXPECT_TRUE( InUnitRange( texel.x ) && InUnitRange( texel.y ) && InUnitRange( texel.z ) &&
                     InUnitRange( texel.w ) )
             << what << " out of [0,1] at (" << x << ", " << y << ", " << z << "): (" << texel.x << ", " << texel.y
             << ", " << texel.z << ", " << texel.w << ")";
    }
} // namespace

// ---------------------------------------------------------------------------------------------------
// 1. Range
//
// The volumes are RGBA8 UNORM. imageStore would clamp anything outside [0,1] silently, so a channel that
// drifts out of range does not crash — it flattens, and the erosion octave it feeds stops eroding.
// ---------------------------------------------------------------------------------------------------

TEST( CloudNoiseValues, ShapeTexelStaysInUnitRange )
{
    for ( int z = 0; z < kGridSteps; ++z )
        for ( int y = 0; y < kGridSteps; ++y )
            for ( int x = 0; x < kGridSteps; ++x )
            {
                const glm::vec3 p( TexelCentre( x, kGridSteps ), TexelCentre( y, kGridSteps ),
                                   TexelCentre( z, kGridSteps ) );
                ExpectTexelInUnitRange( Ref::CloudShapeTexel( p, kSeedA ), "shape texel", p.x, p.y, p.z );
            }
}

TEST( CloudNoiseValues, DetailTexelStaysInUnitRange )
{
    for ( int z = 0; z < kGridSteps; ++z )
        for ( int y = 0; y < kGridSteps; ++y )
            for ( int x = 0; x < kGridSteps; ++x )
            {
                const glm::vec3 p( TexelCentre( x, kGridSteps ), TexelCentre( y, kGridSteps ),
                                   TexelCentre( z, kGridSteps ) );
                ExpectTexelInUnitRange( Ref::CloudDetailTexel( p, kSeedB ), "detail texel", p.x, p.y, p.z );
            }
}

TEST( CloudNoiseValues, CurlTexelStaysInUnitRange )
{
    for ( int y = 0; y < kGridSteps; ++y )
        for ( int x = 0; x < kGridSteps; ++x )
        {
            const glm::vec2 uv( TexelCentre( x, kGridSteps ), TexelCentre( y, kGridSteps ) );
            ExpectTexelInUnitRange( Ref::CloudCurlTexel( uv, kSeedA ), "curl texel", uv.x, uv.y, 0.0f );
        }
}

TEST( CloudNoiseValues, IntermediateFieldsStayInUnitRange )
{
    // The texel functions above end in a clamp, so on their own they would pass for almost any
    // implementation — a range assertion on a clamped value says nothing. The INTERMEDIATES are where a
    // normalisation can genuinely be wrong, and where it does damage: a Perlin FBM that overshoots gets
    // saturated by the Perlin-Worley remap into solid cloud, which reads as "coverage is stuck at 1".
    int clipped = 0;
    int samples = 0;

    for ( int z = 0; z < kGridSteps; ++z )
        for ( int y = 0; y < kGridSteps; ++y )
            for ( int x = 0; x < kGridSteps; ++x )
            {
                const glm::vec3 p( TexelCentre( x, kGridSteps ), TexelCentre( y, kGridSteps ),
                                   TexelCentre( z, kGridSteps ) );

                const float worley       = Ref::CloudWorley( p, 6, kSeedA );
                const float worleyFbm    = Ref::CloudWorleyFbm( p, 4, kSeedA );
                const float perlinFbm    = Ref::CloudPerlinFbm01( p, 4, kSeedA );
                const float perlinWorley = Ref::CloudPerlinWorley( p, 4, kSeedA );

                EXPECT_TRUE( InUnitRange( worley ) ) << "Worley = " << worley;
                EXPECT_TRUE( InUnitRange( worleyFbm ) ) << "Worley FBM = " << worleyFbm;
                EXPECT_TRUE( InUnitRange( perlinFbm ) ) << "Perlin FBM = " << perlinFbm;
                EXPECT_TRUE( InUnitRange( perlinWorley ) ) << "Perlin-Worley = " << perlinWorley;

                if ( perlinFbm <= 0.0f || perlinFbm >= 1.0f )
                    ++clipped;
                ++samples;
            }

    // The clamp inside the Perlin FBM is a guard against a rare overshoot, not the normalisation itself.
    // If a large share of the field sits exactly on 0 or 1, the constant that maps gradient noise into
    // the unit interval is wrong and the clamp is hiding it.
    EXPECT_LT( clipped, samples / 20 ) << clipped << " of " << samples
                                       << " Perlin FBM samples are pinned at an end of the range";
}

TEST( CloudNoiseValues, ChannelsAreNotConstant )
{
    // A range test alone passes for a function that returns 0.5 everywhere. Every channel must actually
    // vary, or the erosion octave it feeds is a texture full of one number.
    glm::vec4 lo( 2.0f ), hi( -1.0f );
    for ( int z = 0; z < kGridSteps; ++z )
        for ( int y = 0; y < kGridSteps; ++y )
            for ( int x = 0; x < kGridSteps; ++x )
            {
                const glm::vec4 t =
                     Ref::CloudShapeTexel( glm::vec3( TexelCentre( x, kGridSteps ), TexelCentre( y, kGridSteps ),
                                                      TexelCentre( z, kGridSteps ) ),
                                           kSeedA );
                lo = glm::min( lo, t );
                hi = glm::max( hi, t );
            }

    EXPECT_GT( hi.x - lo.x, 0.1f ) << "the Perlin-Worley base channel barely varies";
    EXPECT_GT( hi.y - lo.y, 0.1f ) << "erosion octave G barely varies";
    EXPECT_GT( hi.z - lo.z, 0.1f ) << "erosion octave B barely varies";
    EXPECT_GT( hi.w - lo.w, 0.1f ) << "erosion octave A barely varies";
}

// ---------------------------------------------------------------------------------------------------
// 2. Seamless tiling
//
// The shape volume is repeated across tens of kilometres of sky. A discontinuity between the value at
// coordinate 0 and the value at coordinate 1 is a straight line drawn across the cloudscape, once per
// tile, and it is the single defect this generator is most likely to have: it needs EVERY lattice index,
// in every octave of both noise families, to be reduced modulo its period.
//
// The assertion is on the CONTINUOUS function, which is the right statement: the shader evaluates it at
// texel centres, and periodicity of the function is what makes the sampler's wrap-around interpolation
// between the last and first texel continuous too.
// ---------------------------------------------------------------------------------------------------

namespace
{
    // The tolerance is float round-off, not a fudge: opposite faces are computed from the SAME hashed
    // cells, so the two results differ only by the order the products are summed in.
    constexpr float kSeamTolerance = 1e-5f;

    void ExpectSeamless( const glm::vec4& a, const glm::vec4& b, const char* axis, float u, float v )
    {
        EXPECT_NEAR( a.x, b.x, kSeamTolerance ) << "seam on " << axis << " at (" << u << ", " << v << "), ch R";
        EXPECT_NEAR( a.y, b.y, kSeamTolerance ) << "seam on " << axis << " at (" << u << ", " << v << "), ch G";
        EXPECT_NEAR( a.z, b.z, kSeamTolerance ) << "seam on " << axis << " at (" << u << ", " << v << "), ch B";
        EXPECT_NEAR( a.w, b.w, kSeamTolerance ) << "seam on " << axis << " at (" << u << ", " << v << "), ch A";
    }
} // namespace

TEST( CloudNoiseTiling, ShapeVolumeTilesOnEveryAxis )
{
    for ( int b = 0; b < kGridSteps; ++b )
        for ( int a = 0; a < kGridSteps; ++a )
        {
            const float u = TexelCentre( a, kGridSteps );
            const float v = TexelCentre( b, kGridSteps );

            ExpectSeamless( Ref::CloudShapeTexel( glm::vec3( 0.0f, u, v ), kSeedA ),
                            Ref::CloudShapeTexel( glm::vec3( 1.0f, u, v ), kSeedA ), "X", u, v );
            ExpectSeamless( Ref::CloudShapeTexel( glm::vec3( u, 0.0f, v ), kSeedA ),
                            Ref::CloudShapeTexel( glm::vec3( u, 1.0f, v ), kSeedA ), "Y", u, v );
            ExpectSeamless( Ref::CloudShapeTexel( glm::vec3( u, v, 0.0f ), kSeedA ),
                            Ref::CloudShapeTexel( glm::vec3( u, v, 1.0f ), kSeedA ), "Z", u, v );
        }
}

TEST( CloudNoiseTiling, DetailVolumeTilesOnEveryAxis )
{
    for ( int b = 0; b < kGridSteps; ++b )
        for ( int a = 0; a < kGridSteps; ++a )
        {
            const float u = TexelCentre( a, kGridSteps );
            const float v = TexelCentre( b, kGridSteps );

            ExpectSeamless( Ref::CloudDetailTexel( glm::vec3( 0.0f, u, v ), kSeedB ),
                            Ref::CloudDetailTexel( glm::vec3( 1.0f, u, v ), kSeedB ), "X", u, v );
            ExpectSeamless( Ref::CloudDetailTexel( glm::vec3( u, 0.0f, v ), kSeedB ),
                            Ref::CloudDetailTexel( glm::vec3( u, 1.0f, v ), kSeedB ), "Y", u, v );
            ExpectSeamless( Ref::CloudDetailTexel( glm::vec3( u, v, 0.0f ), kSeedB ),
                            Ref::CloudDetailTexel( glm::vec3( u, v, 1.0f ), kSeedB ), "Z", u, v );
        }
}

TEST( CloudNoiseValues, AlligatorStaysInRangeTilesAndCarvesCreases )
{
    // CLD-110. Alligator is the Nubis3 billow base: bright cell interiors, sharp dark creases where two
    // cells' influence balances. Range and tiling are the same contracts every other field here signs;
    // the crease property is asserted as dynamic range — a field that never leaves the middle has no
    // creases to erode with.
    float lo = 1.0f;
    float hi = 0.0f;
    for ( int x = 0; x < kGridSteps; ++x )
    {
        for ( int y = 0; y < kGridSteps; ++y )
        {
            for ( int z = 0; z < kGridSteps; ++z )
            {
                const glm::vec3 p( TexelCentre( x, kGridSteps ), TexelCentre( y, kGridSteps ),
                                   TexelCentre( z, kGridSteps ) );
                const float     a = Ref::CloudAlligator( p, 4, kSeedA );
                const float     c = Ref::CloudCurlyAlligator( p, 4, kSeedA );
                EXPECT_TRUE( InUnitRange( a ) ) << "alligator out of range at " << x << "," << y << "," << z;
                EXPECT_TRUE( InUnitRange( c ) ) << "curly out of range at " << x << "," << y << "," << z;
                lo = glm::min( lo, a );
                hi = glm::max( hi, a );
            }
        }
    }
    EXPECT_LT( lo, 0.05f ) << "no creases: the field never gets dark";
    EXPECT_GT( hi, 0.4f ) << "no cells: the field never gets bright";

    for ( int i = 0; i <= 8; ++i )
    {
        const float u = static_cast<float>( i ) / 8.0f;
        for ( int j = 0; j <= 8; ++j )
        {
            const float v = static_cast<float>( j ) / 8.0f;
            EXPECT_NEAR( Ref::CloudAlligator( glm::vec3( 0.0f, u, v ), 4, kSeedA ),
                         Ref::CloudAlligator( glm::vec3( 1.0f, u, v ), 4, kSeedA ), kSeamTolerance );
            EXPECT_NEAR( Ref::CloudCurlyAlligator( glm::vec3( u, 0.0f, v ), 4, kSeedA ),
                         Ref::CloudCurlyAlligator( glm::vec3( u, 1.0f, v ), 4, kSeedA ), kSeamTolerance );
            EXPECT_NEAR( Ref::CloudAlligatorFbm( glm::vec3( u, v, 0.0f ), 4, kSeedA ),
                         Ref::CloudAlligatorFbm( glm::vec3( u, v, 1.0f ), 4, kSeedA ), kSeamTolerance );
            EXPECT_NEAR( Ref::CloudCurlyAlligatorFbm( glm::vec3( 0.0f, u, v ), 4, kSeedA ),
                         Ref::CloudCurlyAlligatorFbm( glm::vec3( 1.0f, u, v ), 4, kSeedA ), kSeamTolerance );
        }
    }
}

TEST( CloudNoiseTiling, CurlMapTilesOnBothAxes )
{
    // The curl is a derivative taken by central differences. Periodicity here is a statement about the
    // difference too: a potential that wrapped but whose neighbourhood did not would give a correct-
    // looking map with a discontinuous derivative, and the swirls would tear along the seam.
    for ( int a = 0; a < kGridSteps; ++a )
    {
        const float t = TexelCentre( a, kGridSteps );

        ExpectSeamless( Ref::CloudCurlTexel( glm::vec2( 0.0f, t ), kSeedA ),
                        Ref::CloudCurlTexel( glm::vec2( 1.0f, t ), kSeedA ), "U", 0.0f, t );
        ExpectSeamless( Ref::CloudCurlTexel( glm::vec2( t, 0.0f ), kSeedA ),
                        Ref::CloudCurlTexel( glm::vec2( t, 1.0f ), kSeedA ), "V", t, 0.0f );
    }
}

TEST( CloudNoiseTiling, WrappedCoordinatesAgreeWithTheirRepresentative )
{
    // Tiling is not only about the two faces: the raymarch feeds world positions divided by a tile size,
    // so it sees coordinates well outside [0,1] and negative ones. p and p + n must be the same sample
    // for every integer n, or the cloudscape shifts as the camera crosses a tile.
    for ( int a = 0; a < kGridSteps; ++a )
    {
        const float     t = TexelCentre( a, kGridSteps );
        const glm::vec3 p( t, 1.0f - t, 0.5f * t );

        const glm::vec4 base = Ref::CloudShapeTexel( p, kSeedA );
        ExpectSeamless( base, Ref::CloudShapeTexel( p + glm::vec3( 3.0f, 0.0f, 0.0f ), kSeedA ), "+3X", t, 0.0f );
        ExpectSeamless( base, Ref::CloudShapeTexel( p - glm::vec3( 0.0f, 2.0f, 0.0f ), kSeedA ), "-2Y", t, 0.0f );
        ExpectSeamless( base, Ref::CloudShapeTexel( p + glm::vec3( -5.0f, 4.0f, 7.0f ), kSeedA ), "mixed", t,
                        0.0f );
    }
}

// ---------------------------------------------------------------------------------------------------
// 3. Determinism, and the seed actually doing something
// ---------------------------------------------------------------------------------------------------

TEST( CloudNoiseDeterminism, SameSeedGivesBitIdenticalTexels )
{
    // Bit-identical, not "close": the hash is integer arithmetic and the FBM is a fixed sum, so anything
    // less than exact equality would mean a hidden dependency on evaluation order or on uninitialised
    // state. This is the property that makes "reproduce it with seed 7" a usable instruction.
    for ( int z = 0; z < kGridSteps; ++z )
        for ( int y = 0; y < kGridSteps; ++y )
            for ( int x = 0; x < kGridSteps; ++x )
            {
                const glm::vec3 p( TexelCentre( x, kGridSteps ), TexelCentre( y, kGridSteps ),
                                   TexelCentre( z, kGridSteps ) );

                const glm::vec4 first  = Ref::CloudShapeTexel( p, kSeedA );
                const glm::vec4 second = Ref::CloudShapeTexel( p, kSeedA );
                EXPECT_EQ( first.x, second.x );
                EXPECT_EQ( first.y, second.y );
                EXPECT_EQ( first.z, second.z );
                EXPECT_EQ( first.w, second.w );
            }
}

TEST( CloudNoiseDeterminism, EvaluationOrderDoesNotMatter )
{
    // Same coordinates, walked in the opposite direction and interleaved with other seeds. A generator
    // that carried state between calls would pass the test above and fail this one.
    std::vector<glm::vec4> forward;
    std::vector<glm::vec4> backward;

    for ( int i = 0; i < kGridSteps; ++i )
    {
        const float t = TexelCentre( i, kGridSteps );
        forward.push_back( Ref::CloudDetailTexel( glm::vec3( t, t, t ), kSeedA ) );
    }
    for ( int i = kGridSteps - 1; i >= 0; --i )
    {
        const float t = TexelCentre( i, kGridSteps );
        (void)Ref::CloudShapeTexel( glm::vec3( t, 0.25f, 0.75f ), kSeedB );
        backward.push_back( Ref::CloudDetailTexel( glm::vec3( t, t, t ), kSeedA ) );
    }

    ASSERT_EQ( forward.size(), backward.size() );
    for ( size_t i = 0; i < forward.size(); ++i )
    {
        const glm::vec4& f = forward[i];
        const glm::vec4& b = backward[forward.size() - 1 - i];
        EXPECT_EQ( f.x, b.x );
        EXPECT_EQ( f.y, b.y );
        EXPECT_EQ( f.z, b.z );
        EXPECT_EQ( f.w, b.w );
    }
}

TEST( CloudNoiseDeterminism, DifferentSeedsGiveDifferentVolumes )
{
    // The failure this catches: a seed that is hashed in a way that only shifts the field, or is dropped
    // altogether. Either way the Shape Seed slider moves and the sky does not change, which is precisely
    // the dead-setting failure the engineering contract forbids.
    double meanAbsDiff = 0.0;
    int    samples     = 0;
    int    identical   = 0;

    for ( int z = 0; z < kGridSteps; ++z )
        for ( int y = 0; y < kGridSteps; ++y )
            for ( int x = 0; x < kGridSteps; ++x )
            {
                const glm::vec3 p( TexelCentre( x, kGridSteps ), TexelCentre( y, kGridSteps ),
                                   TexelCentre( z, kGridSteps ) );
                const glm::vec4 a = Ref::CloudShapeTexel( p, kSeedA );
                const glm::vec4 b = Ref::CloudShapeTexel( p, kSeedB );

                meanAbsDiff += std::abs( a.x - b.x );
                ++samples;
                if ( a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w )
                    ++identical;
            }

    meanAbsDiff /= static_cast<double>( samples );
    EXPECT_GT( meanAbsDiff, 0.05 ) << "two seeds produce nearly the same base shape channel";
    EXPECT_LT( identical, samples / 100 )
         << identical << " of " << samples << " texels are identical between two seeds";
}

TEST( CloudNoiseDeterminism, CurlIsDrivenByItsSeed )
{
    double meanAbsDiff = 0.0;
    int    samples     = 0;
    for ( int y = 0; y < kGridSteps; ++y )
        for ( int x = 0; x < kGridSteps; ++x )
        {
            const glm::vec2 uv( TexelCentre( x, kGridSteps ), TexelCentre( y, kGridSteps ) );
            const glm::vec4 a = Ref::CloudCurlTexel( uv, kSeedA );
            const glm::vec4 b = Ref::CloudCurlTexel( uv, kSeedB );
            meanAbsDiff += std::abs( a.x - b.x ) + std::abs( a.y - b.y ) + std::abs( a.z - b.z );
            ++samples;
        }
    EXPECT_GT( meanAbsDiff / static_cast<double>( samples ), 0.01 )
         << "the curl map barely changes between two seeds";
}

TEST( CloudNoiseValues, CurlEncodingRoundTripsThroughTheDeclaredScale )
{
    // The RGB channels are a signed vector folded into [0,1] by a FIXED scale. The consumer decodes with
    // the matching constant, so the two must be reciprocal — if they drift, the swirls come out at the
    // wrong strength and there is nothing in the picture that says why.
    EXPECT_FLOAT_EQ( Ref::CLOUD_CURL_ENCODE_SCALE * Ref::CLOUD_CURL_DECODE_SCALE, 1.0f );

    for ( int y = 0; y < kGridSteps; ++y )
        for ( int x = 0; x < kGridSteps; ++x )
        {
            const glm::vec2 uv( TexelCentre( x, kGridSteps ), TexelCentre( y, kGridSteps ) );
            const glm::vec3 curl    = Ref::CloudCurl( uv, kSeedA );
            const glm::vec4 encoded = Ref::CloudCurlTexel( uv, kSeedA );

            // Only where the encoding did not saturate; a saturated component is clamped on purpose.
            if ( encoded.x > 0.0f && encoded.x < 1.0f )
                EXPECT_NEAR( ( encoded.x - 0.5f ) * Ref::CLOUD_CURL_DECODE_SCALE, curl.x, 1e-4f );
            if ( encoded.y > 0.0f && encoded.y < 1.0f )
                EXPECT_NEAR( ( encoded.y - 0.5f ) * Ref::CLOUD_CURL_DECODE_SCALE, curl.y, 1e-4f );

            EXPECT_FLOAT_EQ( encoded.w, 1.0f ) << "the documented constant alpha changed";
        }
}

// The detail volume's MEAN is what a sample past the march's own Nyquist limit is handed instead of an
// instance it cannot reconstruct (Common/CloudDensityProcedural.glslh's erosion gate). There is no mip
// chain on these volumes, so the constant IS the filter, and if the noise construction ever drifts away
// from it the far field would quietly gain or lose mass with no other symptom — a fringe at the horizon
// is what the last disagreement of this kind looked like.
//
// Averaged over a uniform 24^3 grid of texel centres. The mean converges long before the volume's own
// 128^3 resolution — measured at 16, 24, 32 and 48 the twelve channel means all sit inside 0.1209 to
// 0.1298 — and a Debug run of the full grid would cost minutes for three more decimal places nobody
// reads. The tolerance below is that spread with room to spare, and it is deliberately loose enough to
// pass and tight enough that a construction change (an octave, a weight, a warp) fails it.
TEST( CloudNoiseValues, TheDetailVolumesMeanAndSpreadAreTheDeclaredConstants )
{
    constexpr int           kN       = 24;
    constexpr std::uint32_t kSeeds[] = { kSeedA, kSeedB, 991u };

    for ( const std::uint32_t seed : kSeeds )
    {
        double sum[4]       = { 0.0, 0.0, 0.0, 0.0 };
        double sumSquare[4] = { 0.0, 0.0, 0.0, 0.0 };
        for ( int z = 0; z < kN; ++z )
            for ( int y = 0; y < kN; ++y )
                for ( int x = 0; x < kN; ++x )
                {
                    const glm::vec3 p( TexelCentre( x, kN ), TexelCentre( y, kN ), TexelCentre( z, kN ) );
                    const glm::vec4 t    = Ref::CloudDetailTexel( p, seed );
                    const float     c[4] = { t.x, t.y, t.z, t.w };
                    for ( int i = 0; i < 4; ++i )
                    {
                        sum[i] += c[i];
                        sumSquare[i] += static_cast<double>( c[i] ) * c[i];
                    }
                }

        const double n = static_cast<double>( kN ) * kN * kN;
        for ( int c = 0; c < 4; ++c )
        {
            const double mean = sum[c] / n;
            const double sd   = std::sqrt( std::max( sumSquare[c] / n - mean * mean, 0.0 ) );

            EXPECT_NEAR( static_cast<float>( mean ), Ref::CLOUD_DETAIL_MEAN_LEVEL, 0.01f )
                 << "channel " << c << " of seed " << seed << " drifted away from CLOUD_DETAIL_MEAN_LEVEL";
            EXPECT_NEAR( static_cast<float>( sd ), Ref::CLOUD_DETAIL_SD_LEVEL, 0.01f )
                 << "channel " << c << " of seed " << seed << " drifted away from CLOUD_DETAIL_SD_LEVEL";
        }
    }
}

// The two erosion feature sizes are the amplitude-weighted mean of a three-octave stack at periods p,
// 2p, 4p with the weights CloudAlligatorFbm applies. Written out here rather than trusted, because the
// constants are the only place the shader states what frequency its erosion runs at, and an FBM whose
// weights were retuned without them would gate at the wrong distance and say nothing about it.
TEST( CloudNoiseValues, TheErosionFeatureSizesFollowTheFbmWeights )
{
    const auto perTile = []( float basePeriod )
    {
        const float weighted = 0.625f / 1.0f + 0.25f / 2.0f + 0.125f / 4.0f; // = 25/32
        return basePeriod / weighted;
    };

    EXPECT_FLOAT_EQ( Ref::CLOUD_DETAIL_EROSION_LOW_PER_TILE, perTile( 4.0f ) );  // CloudDetailTexel R, B
    EXPECT_FLOAT_EQ( Ref::CLOUD_DETAIL_EROSION_HIGH_PER_TILE, perTile( 8.0f ) ); // CloudDetailTexel G, A

    // The coarse pair must stay resolvable LONGER than the fine pair, or the staged fade the density seam
    // relies on runs backwards and the far field keeps its finest content while losing its coarsest.
    EXPECT_LT( Ref::CLOUD_DETAIL_EROSION_LOW_PER_TILE, Ref::CLOUD_DETAIL_EROSION_HIGH_PER_TILE );
}

// ---------------------------------------------------------------------------------------------------
// 4. The lifecycle rules
// ---------------------------------------------------------------------------------------------------

TEST( CloudNoiseRules, SeedFoldsIntoTheAuthoredRange )
{
    EXPECT_EQ( CloudSeedFromComponent( 0 ), 0u );
    EXPECT_EQ( CloudSeedFromComponent( 7 ), 7u );
    EXPECT_EQ( CloudSeedFromComponent( 65535 ), 65535u );

    // Out of the component's Range(0, 65535). These must be legal seeds, not sign-extended nonsense that
    // lands on a volume the Details panel can never reach again.
    EXPECT_EQ( CloudSeedFromComponent( 65536 ), 0u );
    EXPECT_EQ( CloudSeedFromComponent( -1 ), 65535u );
    EXPECT_EQ( CloudSeedFromComponent( -65536 ), 0u );
}

TEST( CloudNoiseRules, TheKeyIsTheSeedPair )
{
    EXPECT_EQ( MakeCloudNoiseKey( 7, 13 ), MakeCloudNoiseKey( 7, 13 ) );
    EXPECT_NE( MakeCloudNoiseKey( 7, 13 ), MakeCloudNoiseKey( 8, 13 ) );
    EXPECT_NE( MakeCloudNoiseKey( 7, 13 ), MakeCloudNoiseKey( 7, 14 ) );

    // Folding happens before the comparison, so 65536 and 0 are one key and one volume.
    EXPECT_EQ( MakeCloudNoiseKey( 65536, 13 ), MakeCloudNoiseKey( 0, 13 ) );
}

TEST( CloudNoiseRules, CurlSeedFollowsTheDetailSeed )
{
    // Curl warps the DETAIL lookup, so the two have to reshuffle together; and the derivation has to be
    // injective, or two detail seeds would share a swirl pattern.
    EXPECT_NE( CloudCurlSeedFrom( 13u ), CloudCurlSeedFrom( 14u ) );
    EXPECT_EQ( CloudCurlSeedFrom( 13u ), CloudCurlSeedFrom( 13u ) );
    EXPECT_NE( CloudCurlSeedFrom( 13u ), 13u ) << "the curl map would correlate with the detail volume";
}

TEST( CloudNoiseRules, NothingHappensWhenNothingChanges )
{
    const CloudNoiseLease  held{ .Held = true, .Key = MakeCloudNoiseKey( 7, 13 ) };
    const CloudNoiseDemand same{ .Wanted = true, .Key = MakeCloudNoiseKey( 7, 13 ) };
    EXPECT_EQ( DecideCloudNoiseAction( held, same ), CloudNoiseAction::None );

    const CloudNoiseLease  none{};
    const CloudNoiseDemand nothingWanted{};
    EXPECT_EQ( DecideCloudNoiseAction( none, nothingWanted ), CloudNoiseAction::None );
}

TEST( CloudNoiseRules, AComponentAppearingGeneratesTheVolumes )
{
    const CloudNoiseLease  none{};
    const CloudNoiseDemand wanted{ .Wanted = true, .Key = MakeCloudNoiseKey( 7, 13 ) };
    EXPECT_EQ( DecideCloudNoiseAction( none, wanted ), CloudNoiseAction::Generate );
}

TEST( CloudNoiseRules, AComponentDisappearingReleasesTheVolumes )
{
    const CloudNoiseLease  held{ .Held = true, .Key = MakeCloudNoiseKey( 7, 13 ) };
    const CloudNoiseDemand gone{};
    EXPECT_EQ( DecideCloudNoiseAction( held, gone ), CloudNoiseAction::Release );

    // Unticking Enabled is the same thing: an unrendered layer must not keep 8 MiB alive.
    const CloudNoiseDemand disabled{ .Wanted = false, .Key = MakeCloudNoiseKey( 7, 13 ) };
    EXPECT_EQ( DecideCloudNoiseAction( held, disabled ), CloudNoiseAction::Release );
}

TEST( CloudNoiseRules, ASeedChangeRegenerates )
{
    const CloudNoiseLease held{ .Held = true, .Key = MakeCloudNoiseKey( 7, 13 ) };

    const CloudNoiseDemand newShape{ .Wanted = true, .Key = MakeCloudNoiseKey( 8, 13 ) };
    EXPECT_EQ( DecideCloudNoiseAction( held, newShape ), CloudNoiseAction::Regenerate );

    const CloudNoiseDemand newDetail{ .Wanted = true, .Key = MakeCloudNoiseKey( 7, 14 ) };
    EXPECT_EQ( DecideCloudNoiseAction( held, newDetail ), CloudNoiseAction::Regenerate );
}

TEST( CloudNoiseRules, TheTransientRequestRegenerates )
{
    // Same key, but the component asked. This is the path that clears a latched generation failure — the
    // reason the editor's request is not a no-op even though the content is a pure function of the seed.
    const CloudNoiseLease  held{ .Held = true, .Key = MakeCloudNoiseKey( 7, 13 ) };
    const CloudNoiseDemand asked{ .Wanted = true, .Key = MakeCloudNoiseKey( 7, 13 ), .ForceRegenerate = true };
    EXPECT_EQ( DecideCloudNoiseAction( held, asked ), CloudNoiseAction::Regenerate );

    // Asking while nothing is held is still just a first generation, not a regeneration of nothing.
    const CloudNoiseLease none{};
    EXPECT_EQ( DecideCloudNoiseAction( none, asked ), CloudNoiseAction::Generate );

    // Asking while the component is gone releases; the request must not resurrect a set nobody wants.
    const CloudNoiseDemand askedButGone{ .Wanted = false, .ForceRegenerate = true };
    EXPECT_EQ( DecideCloudNoiseAction( held, askedButGone ), CloudNoiseAction::Release );
}

TEST( CloudNoiseRules, TheAdvertisedMemoryCostIsWhatTheVolumesActuallyCost )
{
    // 128^3 * 4 = 8 MiB + 128^3 * 4 = 8 MiB + 128^2 * 4 = 64 KiB. The number goes in the log when a set
    // is generated, so it has to be the real one. Detail moved 32^3 -> 128^3 with CLD-110 (the Nubis3
    // deck's own spec, p.94) — at 32^3 the finest carving the erosion could do was 125 m at the authored
    // tile, which is what rendered every cloud edge as putty.
    constexpr std::uint64_t shape  = 128ull * 128ull * 128ull * 4ull;
    constexpr std::uint64_t detail = 128ull * 128ull * 128ull * 4ull;
    constexpr std::uint64_t curl   = 128ull * 128ull * 4ull;

    EXPECT_EQ( kCloudShapeNoiseSize, 128u );
    EXPECT_EQ( kCloudDetailNoiseSize, 128u );
    EXPECT_EQ( kCloudCurlNoiseSize, 128u );
    EXPECT_EQ( CloudNoiseSetBytes(), shape + detail + curl );
    EXPECT_EQ( CloudNoiseSetBytes(), 16'842'752ull );
}

TEST( CloudNoiseRules, EveryVolumeEdgeDividesTheWorkGroup )
{
    // The dispatch derives its group counts from these; a size that did not divide would leave the last
    // slab of texels unwritten unless the bounds test in the shader fires.
    EXPECT_EQ( kCloudShapeNoiseSize % kCloudNoiseWorkGroupSize, 0u );
    EXPECT_EQ( kCloudDetailNoiseSize % kCloudNoiseWorkGroupSize, 0u );
    EXPECT_EQ( kCloudCurlNoiseSize % kCloudNoiseWorkGroupSize, 0u );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
