// The physical atmosphere's medium, phases and LUT parameterisations, tested against the relations
// that make the Phase 2 sky possible — not against hand-picked spot values.
//
// The functions under test are Editor/Resources/Shaders/Common/SkyMedium.glslh compiled as C++
// (SkyMediumReference.hpp), i.e. the exact text the SkyTransmittanceLut / SkyMultiScatterLut compute
// passes run on the GPU. What is pinned, and why:
//
//   * the UV mappings ROUND-TRIP — a write-side/read-side mapping mismatch is a sky that is subtly
//     wrong everywhere and provably wrong nowhere;
//   * the densities are positive and fall where they must — an inverted exponential renders "something";
//   * the 40-step integrator agrees with a brute-force fine march AND with the closed form the
//     exponential profile admits — two independent references, so an error in the shared sampling
//     cannot vouch for itself;
//   * every phase function integrates to 1 over the sphere — a spurious 4pi is invisible in a picture
//     that is merely "too bright".

#include "SkyMediumReference.hpp"

#include <Engine/Graphic/SkyGroundTransmittance.hpp>
#include <Engine/Graphic/SkyPayload.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Ref = Desert::Tests::SkyMediumRef;

namespace
{
    // Earth, through the REAL packing path: SkySettings' defaults -> PackSky -> SkyMakeAtmParams. The
    // tests therefore exercise the same bytes-in-lanes agreement the GPU depends on, including the
    // world-units-to-kilometres conversion of the planet radius.
    Ref::SkyAtmParams EarthParams()
    {
        const Desert::Graphic::SkySettings   sky{};
        const Desert::Graphic::SkyGpuPayload payload =
             Desert::Graphic::PackSky( glm::vec3( 0.0f, 1.0f, 0.0f ), sky );
        return Ref::SkyMakeAtmParams( payload.MediumRayleigh, payload.MediumMie, payload.MediumMieAbsorption,
                                      payload.MediumOzone, payload.MediumGround, payload.MediumTentPlanet );
    }
} // namespace

// ---------------------------------------------------------------------------------------------------
// The payload's medium block and the shader's params describe the same Earth
// ---------------------------------------------------------------------------------------------------

TEST( SkyMedium, PayloadMediumBlockUnpacksToTheAuthoredEarth )
{
    const Ref::SkyAtmParams p = EarthParams();

    // The one unit conversion: 636 000 000 world units (centimetres) -> 6360 km, inside the shader text.
    EXPECT_FLOAT_EQ( p.BottomRadiusKm, 6360.0f );
    EXPECT_FLOAT_EQ( p.TopRadiusKm, 6420.0f ); // bottom + the 60 km atmosphere height

    // UE's Earth coefficients, already collapsed from scale x colour by MakeSkySettings' convention
    // (the SkySettings defaults hold the products).
    EXPECT_NEAR( p.RayleighScattering.x, 0.005802f, 1e-6f );
    EXPECT_NEAR( p.RayleighScattering.y, 0.013558f, 1e-6f );
    EXPECT_NEAR( p.RayleighScattering.z, 0.033100f, 1e-6f );
    EXPECT_FLOAT_EQ( p.RayleighScaleKm, 8.0f );
    EXPECT_NEAR( p.MieScattering.x, 0.003996f, 1e-6f );
    EXPECT_NEAR( p.MieAbsorption.x, 0.000444f, 1e-6f );
    EXPECT_FLOAT_EQ( p.MieScaleKm, 1.2f );
    EXPECT_FLOAT_EQ( p.MiePhaseG, 0.8f );
    EXPECT_NEAR( p.OzoneAbsorption.y, 0.001881f, 1e-6f );
    EXPECT_FLOAT_EQ( p.OzoneTipAltitudeKm, 25.0f );
    EXPECT_FLOAT_EQ( p.OzoneTipValue, 1.0f );
    EXPECT_FLOAT_EQ( p.OzoneTentWidthKm, 15.0f );
    EXPECT_NEAR( p.GroundAlbedo.x, 0.401978f, 1e-6f );
    EXPECT_FLOAT_EQ( p.MultiScatteringFactor, 1.0f );
}

// ---------------------------------------------------------------------------------------------------
// The transmittance LUT mapping is an exactly invertible pair
// ---------------------------------------------------------------------------------------------------

TEST( SkyMedium, TransmittanceUvRoundTripsAcrossTheDomain )
{
    const Ref::SkyAtmParams p = EarthParams();

    // Every texel centre of the real 256x64 LUT plus the exact corners: uv -> (r, mu) -> uv.
    for ( int y = 0; y <= 64; ++y )
    {
        for ( int x = 0; x <= 256; ++x )
        {
            const glm::vec2 uv( static_cast<float>( x ) / 256.0f, static_cast<float>( y ) / 64.0f );

            const Ref::SkyTransmittanceLutCoord c =
                 Ref::SkyTransmittanceLutParamsFromUv( p.BottomRadiusKm, p.TopRadiusKm, uv );

            EXPECT_GE( c.ViewHeightKm, p.BottomRadiusKm - 1e-2f );
            EXPECT_LE( c.ViewHeightKm, p.TopRadiusKm + 1e-2f );
            EXPECT_GE( c.ViewZenithCos, -1.0f );
            EXPECT_LE( c.ViewZenithCos, 1.0f );

            const glm::vec2 back = Ref::SkyTransmittanceLutUvFromParams( p.BottomRadiusKm, p.TopRadiusKm,
                                                                         c.ViewHeightKm, c.ViewZenithCos );

            EXPECT_NEAR( back.x, uv.x, 2e-3f ) << "uv=(" << uv.x << "," << uv.y << ")";
            EXPECT_NEAR( back.y, uv.y, 2e-3f ) << "uv=(" << uv.x << "," << uv.y << ")";
        }
    }
}

TEST( SkyMedium, TransmittanceParamsRoundTripThroughUv )
{
    const Ref::SkyAtmParams p = EarthParams();

    // (r, mu) -> uv -> (r, mu), over the mapping's actual domain: mu from the HORIZON cosine
    // (mu_h = -rho/r, where the ray grazes the planet) up to 1. Below the horizon the ray enters the
    // planet, the transmittance is identically zero, and Bruneton's distance parameterisation
    // deliberately folds that half onto uv.x = 1 — it is not part of the bijection and no consumer
    // reads it. r stops a kilometre short of the top on purpose: as r -> top the whole upward mu range
    // collapses into a vanishing distance interval (d -> 0 for every mu), so mu is numerically
    // unrecoverable there — and physically irrelevant, the transmittance of that last sliver being ~1
    // in every direction.
    for ( int ri = 0; ri <= 32; ++ri )
    {
        const float r =
             p.BottomRadiusKm + ( static_cast<float>( ri ) / 32.0f ) * ( p.TopRadiusKm - 1.0f - p.BottomRadiusKm );

        const float rho       = std::sqrt( std::max( ( r - p.BottomRadiusKm ) * ( r + p.BottomRadiusKm ), 0.0f ) );
        const float muHorizon = -rho / r;

        for ( int mi = 0; mi <= 64; ++mi )
        {
            const float mu = muHorizon + ( 1.0f - muHorizon ) * static_cast<float>( mi ) / 64.0f;

            const glm::vec2 uv = Ref::SkyTransmittanceLutUvFromParams( p.BottomRadiusKm, p.TopRadiusKm, r, mu );
            const Ref::SkyTransmittanceLutCoord c =
                 Ref::SkyTransmittanceLutParamsFromUv( p.BottomRadiusKm, p.TopRadiusKm, uv );

            EXPECT_NEAR( c.ViewHeightKm, r, 0.05f ) << "r=" << r << " mu=" << mu;
            // 6e-3: float reconstruction error at planetary magnitudes (the mu formula divides
            // ~1e5-sized cancellations by 2*r*d). The LUT's own uv.x texel is 1/256 = 3.9e-3 of the
            // distance domain, so this bound is about one texel — the mapping cannot be more exact
            // than the texture it addresses.
            EXPECT_NEAR( c.ViewZenithCos, mu, 6e-3f ) << "r=" << r << " mu=" << mu;
        }
    }
}

TEST( SkyMedium, MultiScatterMappingAndTexelRemapAreExactInverses )
{
    const Ref::SkyAtmParams p = EarthParams();

    for ( int i = 0; i <= 32; ++i )
    {
        const float unit = static_cast<float>( i ) / 32.0f;

        // The texel-centre remap: unit -> uv -> unit, and texel centres land on exact unit values.
        const float uv = Ref::SkyUnitToTexelUv( unit, 32.0f );
        EXPECT_NEAR( Ref::SkyTexelUvToUnit( uv, 32.0f ), unit, 1e-6f );

        for ( int j = 0; j <= 32; ++j )
        {
            const float unitY = static_cast<float>( j ) / 32.0f;

            const Ref::SkyMultiScatterLutCoord c =
                 Ref::SkyMultiScatterParamsFromUnit( p.BottomRadiusKm, p.TopRadiusKm, unit, unitY );
            const glm::vec2 back = Ref::SkyMultiScatterUnitFromParams( p.BottomRadiusKm, p.TopRadiusKm,
                                                                       c.ViewHeightKm, c.SunZenithCos );

            EXPECT_NEAR( back.x, unit, 1e-5f );
            EXPECT_NEAR( back.y, unitY, 1e-5f );

            // The lift off the exact surface that keeps the ground march non-degenerate.
            EXPECT_GE( c.ViewHeightKm, p.BottomRadiusKm + Ref::SKY_PLANET_RADIUS_OFFSET_KM - 1e-4f );
            EXPECT_LE( c.ViewHeightKm, p.TopRadiusKm + 1e-4f );
        }
    }

    // Texel 0's centre reads unit 0 and the last texel's centre reads unit 1 — the property that makes
    // the write side (SkyTexelUvToUnit of pixel centres) and a future linear-sampling read side agree.
    EXPECT_NEAR( Ref::SkyTexelUvToUnit( 0.5f / 32.0f, 32.0f ), 0.0f, 1e-6f );
    EXPECT_NEAR( Ref::SkyTexelUvToUnit( 31.5f / 32.0f, 32.0f ), 1.0f, 1e-6f );
}

// ---------------------------------------------------------------------------------------------------
// The medium behaves like an atmosphere
// ---------------------------------------------------------------------------------------------------

TEST( SkyMedium, DensitiesArePositiveAndFallWhereTheyMust )
{
    const Ref::SkyAtmParams p = EarthParams();

    float previousRayleigh = 2.0f;
    float previousMie      = 2.0f;
    for ( int i = 0; i <= 60; ++i )
    {
        const float altitude = static_cast<float>( i );

        const float rayleigh = Ref::SkyDensityRayleigh( p, altitude );
        const float mie      = Ref::SkyDensityMie( p, altitude );
        const float ozone    = Ref::SkyDensityOzone( p, altitude );

        EXPECT_GT( rayleigh, 0.0f );
        EXPECT_GT( mie, 0.0f );
        EXPECT_GE( ozone, 0.0f );

        // The exponential profiles fall strictly with altitude — an inverted sign here still renders a
        // sky, just one whose air thickens toward space.
        EXPECT_LT( rayleigh, previousRayleigh ) << "altitude " << altitude;
        EXPECT_LT( mie, previousMie ) << "altitude " << altitude;
        previousRayleigh = rayleigh;
        previousMie      = mie;

        // Extinction can never be below scattering: absorption only ever adds.
        const Ref::SkyMediumSample m = Ref::SkySampleMedium( p, altitude );
        EXPECT_GE( m.Extinction.x, m.Scattering.x );
        EXPECT_GE( m.Extinction.y, m.Scattering.y );
        EXPECT_GE( m.Extinction.z, m.Scattering.z );
    }

    // The ozone tent: peak value at the tip, zero at and beyond tip +/- width, symmetric flanks.
    EXPECT_FLOAT_EQ( Ref::SkyDensityOzone( p, p.OzoneTipAltitudeKm ), p.OzoneTipValue );
    EXPECT_FLOAT_EQ( Ref::SkyDensityOzone( p, p.OzoneTipAltitudeKm - p.OzoneTentWidthKm ), 0.0f );
    EXPECT_FLOAT_EQ( Ref::SkyDensityOzone( p, p.OzoneTipAltitudeKm + p.OzoneTentWidthKm ), 0.0f );
    EXPECT_FLOAT_EQ( Ref::SkyDensityOzone( p, 0.0f ), 0.0f );
    EXPECT_NEAR( Ref::SkyDensityOzone( p, p.OzoneTipAltitudeKm - 5.0f ),
                 Ref::SkyDensityOzone( p, p.OzoneTipAltitudeKm + 5.0f ), 1e-6f );
}

// ---------------------------------------------------------------------------------------------------
// The 40-step integrator against two independent references
// ---------------------------------------------------------------------------------------------------

TEST( SkyMedium, TransmittanceMatchesABruteForceFineMarch )
{
    const Ref::SkyAtmParams p = EarthParams();

    // The LUT's own budget (40 steps, the shader's kTransmittanceSampleCount) against a 20000-step
    // march of the SAME text. Compared as TRANSMITTANCE, the quantity the LUT stores: near the horizon
    // the optical depth grows large while its exp shrinks to nothing, so an absolute bound on T is the
    // honest statement of "the picture cannot tell them apart".
    const float testMus[]       = { 1.0f, 0.7f, 0.3f, 0.1f, 0.0f, -0.05f };
    const float testAltitudes[] = { 0.0f, 0.5f, 2.0f, 8.0f, 30.0f, 59.0f };

    for ( const float altitude : testAltitudes )
    {
        for ( const float mu : testMus )
        {
            const float r = p.BottomRadiusKm + altitude;

            const glm::vec3 coarse = Ref::SkyTransmittanceToTop( p, r, mu, 40 );
            const glm::vec3 fine   = Ref::SkyTransmittanceToTop( p, r, mu, 20000 );

            EXPECT_NEAR( coarse.x, fine.x, 0.015f ) << "alt=" << altitude << " mu=" << mu;
            EXPECT_NEAR( coarse.y, fine.y, 0.015f ) << "alt=" << altitude << " mu=" << mu;
            EXPECT_NEAR( coarse.z, fine.z, 0.015f ) << "alt=" << altitude << " mu=" << mu;
        }
    }
}

TEST( SkyMedium, VerticalOpticalDepthMatchesTheClosedForm )
{
    // A second, INDEPENDENT reference: for a vertical ray the exponential profile integrates in closed
    // form, integral of sigma0 * exp(-h/H) from h0 to hTop = sigma0 * H * (exp(-h0/H) - exp(-hTop/H)).
    // The march never sees this formula, so agreement here is not the sampling vouching for itself.
    const Ref::SkyAtmParams p = EarthParams();

    for ( const float h0 : { 0.0f, 1.0f, 4.0f, 16.0f, 40.0f } )
    {
        const float hTop = p.TopRadiusKm - p.BottomRadiusKm;

        const float rayleighClosed =
             p.RayleighScaleKm * ( std::exp( -h0 / p.RayleighScaleKm ) - std::exp( -hTop / p.RayleighScaleKm ) );
        const float mieClosed =
             p.MieScaleKm * ( std::exp( -h0 / p.MieScaleKm ) - std::exp( -hTop / p.MieScaleKm ) );

        // The ozone tent's closed form: the area of the clipped triangle above h0. Piecewise, so it is
        // evaluated by fine summation of the DENSITY FORMULA only (not the march) — still independent
        // of SkyOpticalDepthToTop's sampling.
        double       ozoneArea = 0.0;
        const double ozoneStep = 0.001;
        for ( double h = h0; h < hTop; h += ozoneStep )
            ozoneArea +=
                 static_cast<double>( Ref::SkyDensityOzone( p, static_cast<float>( h + ozoneStep * 0.5 ) ) ) *
                 ozoneStep;

        const glm::vec3 expected = p.RayleighScattering * rayleighClosed +
                                   ( p.MieScattering + p.MieAbsorption ) * mieClosed +
                                   p.OzoneAbsorption * static_cast<float>( ozoneArea );

        // A vertical ray's radius grows exactly linearly with t, so even the curved-geometry march
        // reduces to the flat integral; 4000 steps of the 60 km column pins it tightly.
        const glm::vec3 marched = Ref::SkyOpticalDepthToTop( p, p.BottomRadiusKm + h0, 1.0f, 4000 );

        EXPECT_NEAR( marched.x, expected.x, 2e-4f ) << "h0=" << h0;
        EXPECT_NEAR( marched.y, expected.y, 2e-4f ) << "h0=" << h0;
        EXPECT_NEAR( marched.z, expected.z, 2e-4f ) << "h0=" << h0;
    }
}

TEST( SkyMedium, TransmittanceIsBoundedAndGrowsWithAltitude )
{
    const Ref::SkyAtmParams p = EarthParams();

    glm::vec3 previous( 0.0f );
    for ( int i = 0; i <= 20; ++i )
    {
        const float     altitude = 3.0f * static_cast<float>( i );
        const glm::vec3 t        = Ref::SkyTransmittanceToTop( p, p.BottomRadiusKm + altitude, 1.0f, 40 );

        // A transmittance is a probability of survival: (0, 1], never amplifying.
        EXPECT_GT( t.x, 0.0f );
        EXPECT_LE( t.x, 1.0f );
        EXPECT_LE( t.y, 1.0f );
        EXPECT_LE( t.z, 1.0f );

        // Less air above a higher start: T must not shrink as the ray starts higher. The monotonicity
        // catches a flipped altitude sign that a spot value never would.
        EXPECT_GE( t.x, previous.x ) << "altitude " << altitude;
        EXPECT_GE( t.y, previous.y ) << "altitude " << altitude;
        EXPECT_GE( t.z, previous.z ) << "altitude " << altitude;
        previous = t;
    }

    // Red outlives blue through Rayleigh air — the physical ordering that makes sunsets red. From the
    // ground toward the horizon the effect is strongest.
    const glm::vec3 horizon = Ref::SkyTransmittanceToTop( p, p.BottomRadiusKm + 0.2f, 0.0f, 40 );
    EXPECT_GT( horizon.x, horizon.y );
    EXPECT_GT( horizon.y, horizon.z );
}

// ---------------------------------------------------------------------------------------------------
// The sun-light coupling: transmittance from the ground toward the sun (UE's PrepareSunLightProxy)
//
// This is the quantity the directional light's colour is multiplied by in SkyModel::PhysicalAtmosphere,
// so the relations below are the relations a viewer sees on lit geometry: a neutral, barely dimmed sun
// overhead, a red and much darker one at the horizon, and a smooth walk between the two.
//
// Graphic::SunTransmittanceAtGround is the ENGINE's function, compiled into this suite (premake5.lua
// says why). It is tested here rather than mirrored because the whole point of the arrangement is that
// there is one implementation: it packs the SkySettings exactly as the GPU reads them and marches the
// same text as the LUT.
// ---------------------------------------------------------------------------------------------------

namespace
{
    glm::vec3 SunDirectionAtElevation( float degrees )
    {
        const float rad = degrees * 3.14159265358979323846f / 180.0f;
        return glm::vec3( std::cos( rad ), std::sin( rad ), 0.0f );
    }
} // namespace

TEST( SkyGroundTransmittance, IsBoundedAndDimsAsTheSunDescends )
{
    const Desert::Graphic::SkySettings sky{};

    glm::vec3 previous( 2.0f );
    for ( const float elevation : { 90.0f, 60.0f, 45.0f, 30.0f, 20.0f, 10.0f, 5.0f, 2.0f, 1.0f, 0.0f } )
    {
        const glm::vec3 t = Desert::Graphic::SunTransmittanceAtGround( sky, SunDirectionAtElevation( elevation ) );

        // A transmittance is a probability of survival — never negative, never amplifying. An
        // amplifying one here would BRIGHTEN the sun at sunset, which is the exact opposite of the
        // effect this coupling exists for.
        for ( int c = 0; c < 3; ++c )
        {
            EXPECT_GT( t[c], 0.0f ) << "elevation " << elevation << " channel " << c;
            EXPECT_LE( t[c], 1.0f ) << "elevation " << elevation << " channel " << c;
        }

        // More air on a slanted path than on a vertical one: strictly monotone in elevation. A flipped
        // sign somewhere in the mapping still produces "a tint", just the wrong one at the wrong hour.
        EXPECT_LT( t.x, previous.x ) << "elevation " << elevation;
        EXPECT_LT( t.y, previous.y ) << "elevation " << elevation;
        EXPECT_LT( t.z, previous.z ) << "elevation " << elevation;
        previous = t;
    }

    // The zenith sun is barely touched and the horizon sun is nearly gone — the two ends the shots are
    // judged at (noon stays neutral, golden hour reddens).
    const glm::vec3 zenith = Desert::Graphic::SunTransmittanceAtGround( sky, SunDirectionAtElevation( 90.0f ) );
    EXPECT_GT( zenith.x, 0.9f );
    EXPECT_GT( zenith.z, 0.7f );

    const glm::vec3 horizon = Desert::Graphic::SunTransmittanceAtGround( sky, SunDirectionAtElevation( 0.0f ) );
    EXPECT_LT( horizon.x, zenith.x );
    EXPECT_LT( horizon.z, 0.01f );
}

TEST( SkyGroundTransmittance, RedensMonotonicallyAsTheSunDescends )
{
    const Desert::Graphic::SkySettings sky{};

    // R/B is the reddening, in one number. It must GROW without exception as the sun goes down: red
    // survives the long path that blue does not, which is the whole reason a sunset is a sunset.
    float previousRatio = 0.0f;
    for ( const float elevation : { 90.0f, 60.0f, 40.0f, 25.0f, 15.0f, 10.0f, 6.0f, 3.0f, 1.0f } )
    {
        const glm::vec3 t = Desert::Graphic::SunTransmittanceAtGround( sky, SunDirectionAtElevation( elevation ) );

        const float ratio = t.x / t.z;
        EXPECT_GT( ratio, previousRatio ) << "elevation " << elevation << " ratio " << ratio;
        previousRatio = ratio;

        // Red always outlives green, which always outlives blue: the ordering Rayleigh's 1/lambda^4
        // imposes, and the one that decides WHICH way the light shifts.
        EXPECT_GT( t.x, t.y ) << "elevation " << elevation;
        EXPECT_GT( t.y, t.z ) << "elevation " << elevation;
    }

    // Overhead the sun is close to neutral (a few per cent of warm cast), at golden hour it is
    // unmistakably warm. Numbers, not adjectives, so "noon must stay neutral" is a pinned claim.
    const glm::vec3 noon = Desert::Graphic::SunTransmittanceAtGround( sky, SunDirectionAtElevation( 90.0f ) );
    EXPECT_LT( noon.x / noon.z, 1.35f );

    const glm::vec3 golden = Desert::Graphic::SunTransmittanceAtGround( sky, SunDirectionAtElevation( 5.0f ) );
    EXPECT_GT( golden.x / golden.z, 3.0f );
}

TEST( SkyGroundTransmittance, AgreesWithTheTransmittanceLutItShares )
{
    // THE TWO-IMPLEMENTATIONS PROPERTY. The same quantity reaches a frame twice: through this CPU
    // evaluation (the light's colour) and through the 256x64 LUT the GPU samples (the sky's own
    // scattering, the sun disc, the aerial perspective). They are the same text, but not the same path:
    // one marches (r, mu) directly, the other marches Bruneton's uv grid and then interpolates. If those
    // disagree, the sun on the geometry and the sun in the sky part ways — and neither is provably
    // wrong on its own.
    const Desert::Graphic::SkySettings sky{};

    const Ref::SkyAtmParams p = EarthParams();

    // The LUT exactly as SkyTransmittanceLut.shader writes it: one texel per (uv) centre, the shared
    // sample budget, no texel-centre remap (Bruneton's mapping already puts the domain ends on the
    // edge texels).
    constexpr int kWidth  = 256;
    constexpr int kHeight = 64;

    std::vector<glm::vec3> lut( static_cast<size_t>( kWidth * kHeight ) );
    for ( int y = 0; y < kHeight; ++y )
    {
        for ( int x = 0; x < kWidth; ++x )
        {
            const glm::vec2 uv( ( static_cast<float>( x ) + 0.5f ) / kWidth,
                                ( static_cast<float>( y ) + 0.5f ) / kHeight );

            const Ref::SkyTransmittanceLutCoord c =
                 Ref::SkyTransmittanceLutParamsFromUv( p.BottomRadiusKm, p.TopRadiusKm, uv );
            lut[static_cast<size_t>( y * kWidth + x )] = Ref::SkyTransmittanceToTop(
                 p, c.ViewHeightKm, c.ViewZenithCos, Ref::SKY_TRANSMITTANCE_SAMPLE_COUNT );
        }
    }

    // ... and read exactly as a linear sampler would: bilinear on the texel grid, clamped at the edges.
    const auto sample = [&lut]( glm::vec2 uv ) -> glm::vec3
    {
        const float fx = glm::clamp( uv.x * kWidth - 0.5f, 0.0f, static_cast<float>( kWidth - 1 ) );
        const float fy = glm::clamp( uv.y * kHeight - 0.5f, 0.0f, static_cast<float>( kHeight - 1 ) );

        const int x0 = static_cast<int>( fx );
        const int y0 = static_cast<int>( fy );
        const int x1 = std::min( x0 + 1, kWidth - 1 );
        const int y1 = std::min( y0 + 1, kHeight - 1 );

        const float tx = fx - static_cast<float>( x0 );
        const float ty = fy - static_cast<float>( y0 );

        const glm::vec3 a = lut[static_cast<size_t>( y0 * kWidth + x0 )];
        const glm::vec3 b = lut[static_cast<size_t>( y0 * kWidth + x1 )];
        const glm::vec3 c = lut[static_cast<size_t>( y1 * kWidth + x0 )];
        const glm::vec3 d = lut[static_cast<size_t>( y1 * kWidth + x1 )];

        return glm::mix( glm::mix( a, b, tx ), glm::mix( c, d, tx ), ty );
    };

    for ( const float elevation : { 90.0f, 60.0f, 45.0f, 30.0f, 20.0f, 12.0f, 8.0f, 5.0f, 3.0f, 1.0f } )
    {
        const glm::vec3 towardSun = SunDirectionAtElevation( elevation );

        const glm::vec3 cpu = Desert::Graphic::SunTransmittanceAtGround( sky, towardSun );

        const glm::vec2 uv = Ref::SkyTransmittanceLutUvFromParams(
             p.BottomRadiusKm, p.TopRadiusKm, p.BottomRadiusKm + Ref::SKY_PLANET_RADIUS_OFFSET_KM, towardSun.y );
        const glm::vec3 gpu = sample( uv );

        // 0.01 of a transmittance is well under one display level on an 8-bit output, and it is the
        // honest bound: the LUT is 256x64 texels of a function that falls off a cliff at the horizon,
        // so the interpolation error IS the disagreement, and it must stay smaller than the effect.
        EXPECT_NEAR( cpu.x, gpu.x, 0.01f ) << "elevation " << elevation;
        EXPECT_NEAR( cpu.y, gpu.y, 0.01f ) << "elevation " << elevation;
        EXPECT_NEAR( cpu.z, gpu.z, 0.01f ) << "elevation " << elevation;
    }
}

// ---------------------------------------------------------------------------------------------------
// Phase functions: normalized over the sphere, forward-peaked where g says so
// ---------------------------------------------------------------------------------------------------

namespace
{
    // Integral over the sphere = 2pi * integral over cosTheta in [-1, 1] (azimuthal symmetry),
    // midpoint rule. 200000 cells resolves even the g = 0.9 forward spike.
    template <typename Phase>
    double IntegrateOverSphere( Phase&& phase )
    {
        const int    cells = 200000;
        const double dc    = 2.0 / cells;
        double       sum   = 0.0;
        for ( int i = 0; i < cells; ++i )
        {
            const double c = -1.0 + ( i + 0.5 ) * dc;
            sum += static_cast<double>( phase( static_cast<float>( c ) ) ) * dc;
        }
        return 2.0 * 3.14159265358979323846 * sum;
    }
} // namespace

TEST( SkyMedium, PhaseFunctionsIntegrateToOneOverTheSphere )
{
    EXPECT_NEAR( IntegrateOverSphere( []( float ) { return Ref::SkyPhaseUniform(); } ), 1.0, 1e-3 );
    EXPECT_NEAR( IntegrateOverSphere( []( float c ) { return Ref::SkyPhaseRayleigh( c ); } ), 1.0, 1e-3 );

    for ( const float g : { 0.0f, 0.3f, 0.8f, 0.9f } )
    {
        EXPECT_NEAR( IntegrateOverSphere( [g]( float c ) { return Ref::SkyPhaseHenyeyGreenstein( g, c ); } ), 1.0,
                     2e-3 )
             << "HG g=" << g;
        EXPECT_NEAR( IntegrateOverSphere( [g]( float c ) { return Ref::SkyPhaseCornetteShanks( g, c ); } ), 1.0,
                     2e-3 )
             << "CS g=" << g;
    }
}

TEST( SkyMedium, PositiveAnisotropyScattersForward )
{
    // The convention pin: cosTheta = +1 is forward. A flipped sign here puts the sun's halo at the
    // antisolar point — a bug a normalization test cannot see, because the integral is symmetric in it.
    EXPECT_GT( Ref::SkyPhaseHenyeyGreenstein( 0.8f, 1.0f ), Ref::SkyPhaseHenyeyGreenstein( 0.8f, -1.0f ) );
    EXPECT_GT( Ref::SkyPhaseCornetteShanks( 0.8f, 1.0f ), Ref::SkyPhaseCornetteShanks( 0.8f, -1.0f ) );

    // g = 0 collapses both lobes to isotropy-like symmetry.
    EXPECT_NEAR( Ref::SkyPhaseHenyeyGreenstein( 0.0f, 0.5f ), Ref::SkyPhaseUniform(), 1e-6f );
}

// ---------------------------------------------------------------------------------------------------
// Sphere sampling and ray/sphere geometry
// ---------------------------------------------------------------------------------------------------

TEST( SkyMedium, UniformSphereDirectionsAreUnitAndBalanced )
{
    glm::vec3 mean( 0.0f );
    for ( int i = 0; i < 8; ++i )
    {
        for ( int j = 0; j < 8; ++j )
        {
            const float u = ( static_cast<float>( i ) + 0.5f ) / 8.0f;
            const float v = ( static_cast<float>( j ) + 0.5f ) / 8.0f;

            const glm::vec3 dir = Ref::SkyUniformSphereDirection( u, v );
            EXPECT_NEAR( glm::length( dir ), 1.0f, 1e-5f );
            mean += dir;
        }
    }

    // The 8x8 grid is symmetric in azimuth and in zenith cosine, so a uniform distribution sums to the
    // origin. A biased mapping (the classic non-area-uniform acos-less mistake) does not.
    mean /= 64.0f;
    EXPECT_NEAR( mean.x, 0.0f, 1e-5f );
    EXPECT_NEAR( mean.y, 0.0f, 1e-5f );
    EXPECT_NEAR( mean.z, 0.0f, 1e-5f );
}

TEST( SkyMedium, RaySphereDistancesAgreeWithTheGeometry )
{
    const Ref::SkyAtmParams p = EarthParams();

    // Straight up from the ground: exactly the atmosphere height. Straight down from 1 km: exactly 1 km.
    EXPECT_NEAR( Ref::SkyDistanceToTop( p.BottomRadiusKm, 1.0f, p.TopRadiusKm ), p.TopRadiusKm - p.BottomRadiusKm,
                 1e-2f );
    EXPECT_NEAR( Ref::SkyDistanceToBottom( p.BottomRadiusKm + 1.0f, -1.0f, p.BottomRadiusKm ), 1.0f, 1e-2f );

    // From the top looking up there is nothing left to cross.
    EXPECT_NEAR( Ref::SkyDistanceToTop( p.TopRadiusKm, 1.0f, p.TopRadiusKm ), 0.0f, 1e-2f );

    // A horizontal ray from above the surface never hits the planet; a downward one does, and the
    // ground is always nearer than the top beyond it.
    EXPECT_FALSE( Ref::SkyIntersectsGround( p.BottomRadiusKm + 1.0f, 0.0f, p.BottomRadiusKm ) );
    EXPECT_TRUE( Ref::SkyIntersectsGround( p.BottomRadiusKm + 1.0f, -0.5f, p.BottomRadiusKm ) );
    EXPECT_LT( Ref::SkyDistanceToBottom( p.BottomRadiusKm + 1.0f, -0.5f, p.BottomRadiusKm ),
               Ref::SkyDistanceToTop( p.BottomRadiusKm + 1.0f, -0.5f, p.TopRadiusKm ) );

    // The radius along a vertical ray is linear in t — the anchor for the closed-form test above.
    EXPECT_NEAR( Ref::SkyRadiusAlongRay( p.BottomRadiusKm, 1.0f, 12.5f ), p.BottomRadiusKm + 12.5f, 1e-2f );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
