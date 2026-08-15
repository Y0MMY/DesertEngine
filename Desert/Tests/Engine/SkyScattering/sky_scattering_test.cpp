// The Phase 2 sky — the Sky-View LUT parameterisation, the single-scattering integrator and the sun
// disc — tested against the relations that make the physical sky trustworthy, not hand-picked spot
// values.
//
// The functions under test are Editor/Resources/Shaders/Common/SkyScattering.glslh compiled as C++
// (SkyScatteringReference.hpp), i.e. the exact text the SkyViewLut / BakeProceduralSky /
// ProceduralSky shaders run on the GPU. What is pinned, and why:
//
//   * the horizon-warped UV mapping ROUND-TRIPS, in both directions, at several altitudes — the fill
//     writes through one side and the sky pass reads through the other, and a mismatch is a sky that
//     is subtly displaced everywhere and provably wrong nowhere;
//   * the split lands EXACTLY on the horizon — the one angle the warp exists to protect;
//   * the 32-sample integrator agrees with a brute-force fine march of the same medium — the
//     sampling cannot vouch for itself;
//   * the planet's shadow still answers what the hard `SkyIntersectsGround` step answered everywhere
//     outside its own fade band, rises monotonically through it, and is narrower than the sun's disc
//     — the fade exists to stop a term jumping the whole way inside one march segment, and it must
//     not become a second way to brighten twilight;
//   * energy sanity — non-negative everywhere, transmittance a survival probability, the Mie forward
//     lobe brighter toward the sun than away from it, the multi-scattering term linear in Psi;
//   * the sun disc's limb darkening is 1 at the centre, falls to the rim, and reddens there.

#include "SkyScatteringReference.hpp"

#include <Engine/Graphic/SkyPayload.hpp>

#include <gtest/gtest.h>

#include <cmath>

namespace Ref = Desert::Tests::SkyScatteringRef;

namespace
{
    // Earth, through the REAL packing path (the SkyMedium suite's arrangement): SkySettings' defaults
    // -> PackSky -> SkyMakeAtmParams, world-units planet radius converted inside the shader text.
    Ref::SkyAtmParams EarthParams()
    {
        const Desert::Graphic::SkySettings   sky{};
        const Desert::Graphic::SkyGpuPayload payload =
             Desert::Graphic::PackSky( glm::vec3( 0.0f, 1.0f, 0.0f ), sky );
        return Ref::SkyMakeAtmParams( payload.MediumRayleigh, payload.MediumMie, payload.MediumMieAbsorption,
                                      payload.MediumOzone, payload.MediumGround, payload.MediumTentPlanet );
    }

    // Every test that touches the integrator resets the LUT stand-ins first: the defaults are the
    // exact (marched) sun transmittance and NO multi-scattering, which is the configuration the
    // brute-force comparison needs.
    void ResetLutCallbacks()
    {
        Ref::g_SunTransmittance = []( Ref::SkyAtmParams p, float radiusKm, float sunZenithCos )
        { return Ref::SkyTransmittanceToTop( p, radiusKm, sunZenithCos, 40 ); };
        Ref::g_MultiScatter = []( Ref::SkyAtmParams, float, float ) { return glm::vec3( 0.0f, 0.0f, 0.0f ); };
    }

    // A brute-force single-scattering march: uniform steps, midpoint rule, running optical depth, a
    // finely-marched sun transmittance per step. Never calls the integrator under test — an
    // independent statement of the same physics.
    glm::vec3 BruteForceSingleScattering( const Ref::SkyAtmParams& p, const glm::vec3& originKm,
                                          const glm::vec3& rayDir, const glm::vec3& sunDir,
                                          const glm::vec3& sunIlluminance, int stepCount )
    {
        const float r0 = glm::length( originKm );
        const float mu = glm::dot( originKm, rayDir ) / r0;

        const bool  hitsGround = Ref::SkyIntersectsGround( r0, mu, p.BottomRadiusKm );
        const float tMax       = hitsGround ? Ref::SkyDistanceToBottom( r0, mu, p.BottomRadiusKm )
                                            : Ref::SkyDistanceToTop( r0, mu, p.TopRadiusKm );

        const float cosTheta = glm::dot( rayDir, sunDir );
        const float phaseR   = Ref::SkyPhaseRayleigh( cosTheta );
        const float phaseM   = Ref::SkyPhaseCornetteShanks( p.MiePhaseG, cosTheta );

        const float dt = tMax / static_cast<float>( stepCount );

        glm::vec3 luminance( 0.0f );
        glm::vec3 opticalDepth( 0.0f );
        for ( int i = 0; i < stepCount; ++i )
        {
            const float     t        = ( static_cast<float>( i ) + 0.5f ) * dt;
            const glm::vec3 position = originKm + rayDir * t;
            const float     radius   = glm::length( position );
            const float     altitude = radius - p.BottomRadiusKm;

            const float dRay = Ref::SkyDensityRayleigh( p, altitude );
            const float dMie = Ref::SkyDensityMie( p, altitude );
            const float dOzo = Ref::SkyDensityOzone( p, altitude );

            const glm::vec3 scatR = p.RayleighScattering * dRay;
            const glm::vec3 scatM = p.MieScattering * dMie;
            const glm::vec3 ext   = scatR + scatM + p.MieAbsorption * dMie + p.OzoneAbsorption * dOzo;

            const glm::vec3 viewT = glm::exp( -( opticalDepth + ext * ( dt * 0.5f ) ) );

            const glm::vec3 zenith       = position / radius;
            const float     sunZenithCos = glm::dot( zenith, sunDir );
            const float shadow = Ref::SkyIntersectsGround( radius, sunZenithCos, p.BottomRadiusKm ) ? 0.0f : 1.0f;
            const glm::vec3 sunT = Ref::SkyTransmittanceToTop( p, radius, sunZenithCos, 200 );

            luminance += sunIlluminance * shadow * viewT * sunT * ( phaseR * scatR + phaseM * scatM ) * dt;
            opticalDepth += ext * dt;
        }
        return luminance;
    }
} // namespace

// ---------------------------------------------------------------------------------------------------
// The Sky-View LUT mapping is an exactly invertible pair, at every altitude the camera can hold
// ---------------------------------------------------------------------------------------------------

TEST( SkyScattering, SkyViewUnitRoundTripsAcrossTheDomain )
{
    const Ref::SkyAtmParams p = EarthParams();

    for ( const float altitude : { 0.05f, 0.5f, 2.0f, 8.0f, 30.0f } )
    {
        const float r = p.BottomRadiusKm + altitude;

        // Every texel centre of the real 192x104 LUT plus the exact corners: unit -> params -> unit.
        for ( int y = 0; y <= 104; ++y )
        {
            for ( int x = 0; x <= 192; ++x )
            {
                const glm::vec2 unit( static_cast<float>( x ) / 192.0f, static_cast<float>( y ) / 104.0f );

                const Ref::SkyViewLutCoord c = Ref::SkyViewParamsFromUnit( p.BottomRadiusKm, r, unit );

                EXPECT_GE( c.ViewZenithCos, -1.0f );
                EXPECT_LE( c.ViewZenithCos, 1.0f );
                EXPECT_GE( c.LightViewCos, -1.0f );
                EXPECT_LE( c.LightViewCos, 1.0f );

                const glm::vec2 back =
                     Ref::SkyViewUnitFromParams( p.BottomRadiusKm, r, c.ViewZenithCos, c.LightViewCos );

                // 2e-3 of the unit domain is about a fifth of a texel at 104 rows: acos/cos round
                // trips through float, and the sqrt warp amplifies that near its flat ends.
                EXPECT_NEAR( back.x, unit.x, 2e-3f )
                     << "alt=" << altitude << " unit=(" << unit.x << "," << unit.y << ")";
                EXPECT_NEAR( back.y, unit.y, 2e-3f )
                     << "alt=" << altitude << " unit=(" << unit.x << "," << unit.y << ")";
            }
        }
    }
}

TEST( SkyScattering, SkyViewParamsRoundTripThroughUnit )
{
    const Ref::SkyAtmParams p = EarthParams();

    for ( const float altitude : { 0.05f, 1.0f, 10.0f } )
    {
        const float r = p.BottomRadiusKm + altitude;

        for ( int zi = 0; zi <= 64; ++zi )
        {
            const float viewZenithCos = -1.0f + 2.0f * static_cast<float>( zi ) / 64.0f;
            for ( int li = 0; li <= 16; ++li )
            {
                const float lightViewCos = -1.0f + 2.0f * static_cast<float>( li ) / 16.0f;

                const glm::vec2 unit =
                     Ref::SkyViewUnitFromParams( p.BottomRadiusKm, r, viewZenithCos, lightViewCos );
                const Ref::SkyViewLutCoord c = Ref::SkyViewParamsFromUnit( p.BottomRadiusKm, r, unit );

                // The warp compresses toward the horizon, so the honest bound is on the ANGLE the
                // mapping actually transports, not its cosine.
                EXPECT_NEAR( std::acos( glm::clamp( c.ViewZenithCos, -1.0f, 1.0f ) ),
                             std::acos( glm::clamp( viewZenithCos, -1.0f, 1.0f ) ), 3e-3f )
                     << "alt=" << altitude << " mu=" << viewZenithCos;
                EXPECT_NEAR( c.LightViewCos, lightViewCos, 3e-3f ) << "alt=" << altitude << " lv=" << lightViewCos;
            }
        }
    }
}

TEST( SkyScattering, TheSplitSitsExactlyOnTheHorizon )
{
    const Ref::SkyAtmParams p = EarthParams();
    const float             r = p.BottomRadiusKm + 2.0f;

    const float horizonCos = Ref::SkyViewHorizonCos( p.BottomRadiusKm, r );

    // unit.y = 0.5 decodes to the horizon itself; a hair to either side lands on the matching half.
    const Ref::SkyViewLutCoord atSplit =
         Ref::SkyViewParamsFromUnit( p.BottomRadiusKm, r, glm::vec2( 0.5f, 0.5f ) );
    EXPECT_NEAR( atSplit.ViewZenithCos, horizonCos, 1e-4f );

    const Ref::SkyViewLutCoord justSky =
         Ref::SkyViewParamsFromUnit( p.BottomRadiusKm, r, glm::vec2( 0.5f, 0.45f ) );
    const Ref::SkyViewLutCoord justGround =
         Ref::SkyViewParamsFromUnit( p.BottomRadiusKm, r, glm::vec2( 0.5f, 0.55f ) );
    EXPECT_FALSE( Ref::SkyViewHitsGround( p.BottomRadiusKm, r, justSky.ViewZenithCos ) );
    EXPECT_TRUE( Ref::SkyViewHitsGround( p.BottomRadiusKm, r, justGround.ViewZenithCos ) );

    // And the geometric truth the flag stands for: the analytic sphere test agrees with the angular
    // threshold on both sides of it.
    EXPECT_TRUE( Ref::SkyIntersectsGround( r, horizonCos - 1e-4f, p.BottomRadiusKm ) );
    EXPECT_FALSE( Ref::SkyIntersectsGround( r, horizonCos + 1e-4f, p.BottomRadiusKm ) );
}

TEST( SkyScattering, TexelRemapIsExactAtTheLutResolution )
{
    // The write side (SkyViewLut.shader) applies SkyTexelUvToUnit to texel centres, the read side
    // (ProceduralSky.shader) applies SkyUnitToTexelUv — pinned at the LUT's own 192x104 so the two
    // constants and the remap stay one system.
    EXPECT_FLOAT_EQ( Ref::SKY_VIEW_LUT_WIDTH, 192.0f );
    EXPECT_FLOAT_EQ( Ref::SKY_VIEW_LUT_HEIGHT, 104.0f );

    for ( int i = 0; i <= 32; ++i )
    {
        const float unit = static_cast<float>( i ) / 32.0f;
        EXPECT_NEAR( Ref::SkyTexelUvToUnit( Ref::SkyUnitToTexelUv( unit, Ref::SKY_VIEW_LUT_WIDTH ),
                                            Ref::SKY_VIEW_LUT_WIDTH ),
                     unit, 1e-6f );
        EXPECT_NEAR( Ref::SkyTexelUvToUnit( Ref::SkyUnitToTexelUv( unit, Ref::SKY_VIEW_LUT_HEIGHT ),
                                            Ref::SKY_VIEW_LUT_HEIGHT ),
                     unit, 1e-6f );
    }

    // Texel 0's centre holds unit 0, the last texel's centre unit 1 — the ends of the warp's domain.
    EXPECT_NEAR( Ref::SkyTexelUvToUnit( 0.5f / 104.0f, 104.0f ), 0.0f, 1e-6f );
    EXPECT_NEAR( Ref::SkyTexelUvToUnit( 103.5f / 104.0f, 104.0f ), 1.0f, 1e-6f );
}

// ---------------------------------------------------------------------------------------------------
// The camera-to-planet-frame helpers
// ---------------------------------------------------------------------------------------------------

TEST( SkyScattering, ViewHeightAndZenithRecoverTheCameraExactly )
{
    const Ref::SkyAtmParams p = EarthParams();

    // On the surface at the origin: r = Rb, zenith = +Y.
    EXPECT_NEAR( Ref::SkyViewHeightKm( glm::vec3( 0.0f ), p.BottomRadiusKm ), p.BottomRadiusKm, 1e-3f );
    const glm::vec3 zenithAtOrigin = Ref::SkyViewZenith( glm::vec3( 0.0f ), p.BottomRadiusKm );
    EXPECT_NEAR( zenithAtOrigin.y, 1.0f, 1e-6f );

    // Straight up 2 km: r = Rb + 2 exactly (the factored form does not cancel).
    EXPECT_NEAR( Ref::SkyViewHeightKm( glm::vec3( 0.0f, 2.0f, 0.0f ), p.BottomRadiusKm ), p.BottomRadiusKm + 2.0f,
                 1e-3f );

    // 30 km sideways at the surface: against the double-precision law of cosines. This is the case
    // the naive sqrt(r^2) formulation loses to float cancellation.
    const glm::vec3 sideways( 30.0f, 0.0f, 0.0f );
    const double    rd = std::sqrt( 30.0 * 30.0 + static_cast<double>( p.BottomRadiusKm ) *
                                                       static_cast<double>( p.BottomRadiusKm ) );
    EXPECT_NEAR( Ref::SkyViewHeightKm( sideways, p.BottomRadiusKm ), static_cast<float>( rd ), 1e-3f );

    // The zenith there tilts by exactly atan(30 / Rb).
    const glm::vec3 zenithSideways = Ref::SkyViewZenith( sideways, p.BottomRadiusKm );
    EXPECT_NEAR( zenithSideways.x, 30.0f / static_cast<float>( rd ), 1e-5f );
}

TEST( SkyScattering, LightViewCosAndRayDirectionAgree )
{
    // Build a ray FROM (viewZenithCos, lightViewCos), then measure its light-view cosine back with
    // the world-frame helper — the fill writes through one function and the sky pass reads through
    // the other, so they must be inverse views of the same geometry.
    const glm::vec3 zenith( 0.0f, 1.0f, 0.0f );

    for ( int zi = 1; zi < 16; ++zi )
    {
        const float viewZenithCos = -0.95f + 1.9f * static_cast<float>( zi ) / 16.0f;
        for ( int li = 0; li <= 8; ++li )
        {
            const float lightViewCos = -1.0f + 2.0f * static_cast<float>( li ) / 8.0f;

            const glm::vec3 dir = Ref::SkyViewRayDirection( viewZenithCos, lightViewCos );
            EXPECT_NEAR( glm::length( dir ), 1.0f, 1e-5f );
            EXPECT_NEAR( dir.y, viewZenithCos, 1e-5f );

            // The sun of the LUT's local frame lies toward +X.
            const glm::vec3 sunDir    = glm::normalize( glm::vec3( 0.8f, 0.6f, 0.0f ) );
            const float     roundTrip = Ref::SkyViewLightViewCos( zenith, dir, sunDir );
            EXPECT_NEAR( roundTrip, lightViewCos, 1e-4f ) << "mu=" << viewZenithCos << " lv=" << lightViewCos;
        }
    }
}

// ---------------------------------------------------------------------------------------------------
// The planet's shadow on the air
// ---------------------------------------------------------------------------------------------------

// The fade replaced a hard `SkyIntersectsGround` step, so the first thing to pin is that it still
// AGREES with that step everywhere except inside its own band — a fade that answered differently in
// broad daylight would be a new sky, not an artefact fix.
TEST( SkyScattering, PlanetShadowAgreesWithTheHardTestAwayFromTheTerminator )
{
    const Ref::SkyAtmParams p = EarthParams();

    // From the offset the whole medium is parameterised against upward — a sample never sits exactly
    // ON the surface, and the hard test's discriminant is degenerate there (see the case below).
    for ( const float altitudeKm : { Ref::SKY_PLANET_RADIUS_OFFSET_KM, 5.0f, 20.0f, 55.0f } )
    {
        const float radius = p.BottomRadiusKm + altitudeKm;

        for ( int i = 0; i <= 400; ++i )
        {
            const float sunZenithCos = -1.0f + 2.0f * static_cast<float>( i ) / 400.0f;
            const float faded        = Ref::SkyPlanetShadow( radius, sunZenithCos, p.BottomRadiusKm );

            // Distance from this sample's own horizon, in the same cosine units as the band.
            const float rho =
                 std::sqrt( std::max( ( radius - p.BottomRadiusKm ) * ( radius + p.BottomRadiusKm ), 0.0f ) );
            const float horizonCos = -rho / radius;

            if ( std::abs( sunZenithCos - horizonCos ) <= Ref::SKY_TERMINATOR_HALF_WIDTH_COS )
                continue; // inside the band the two are ALLOWED to differ — that is the whole point

            const float hard = Ref::SkyIntersectsGround( radius, sunZenithCos, p.BottomRadiusKm ) ? 0.0f : 1.0f;
            EXPECT_FLOAT_EQ( faded, hard ) << "altitude " << altitudeKm << " km, sun zenith cos " << sunZenithCos;
        }
    }
}

// EXACTLY on the surface the hard test does not merely differ, it is WRONG: rho is zero, so the ray
// to the sun grazes, the discriminant is (r*mu)^2 and sqrt() rounds a hair above |r*mu| — the
// distance comes out negative and a sample with the sun a degree UNDER its feet reports full sun.
// The fade has no discriminant and answers 0. Pinned because it is the direction of the difference
// that matters: replacing the step did not weaken this case, it repaired it.
TEST( SkyScattering, PlanetShadowIsCorrectWhereTheHardTestIsDegenerate )
{
    const Ref::SkyAtmParams p = EarthParams();

    for ( const float sunZenithCos : { -0.99f, -0.5f, -0.1f } )
    {
        EXPECT_FLOAT_EQ( Ref::SkyPlanetShadow( p.BottomRadiusKm, sunZenithCos, p.BottomRadiusKm ), 0.0f )
             << "the sun is below this sample's horizon at cos " << sunZenithCos;
    }
    for ( const float sunZenithCos : { 0.1f, 0.5f, 0.99f } )
    {
        EXPECT_FLOAT_EQ( Ref::SkyPlanetShadow( p.BottomRadiusKm, sunZenithCos, p.BottomRadiusKm ), 1.0f )
             << "the sun is above this sample's horizon at cos " << sunZenithCos;
    }
}

// A monotonic fade, bounded to [0, 1]: the artefact it exists to remove came from a term that could
// jump the whole way inside one march segment, so what must hold is that light never DECREASES as the
// sun rises, and that no sample is ever more than fully lit.
TEST( SkyScattering, PlanetShadowRisesMonotonicallyThroughTheTerminator )
{
    const Ref::SkyAtmParams p      = EarthParams();
    const float             radius = p.BottomRadiusKm + 10.0f;

    const float rho = std::sqrt( std::max( ( radius - p.BottomRadiusKm ) * ( radius + p.BottomRadiusKm ), 0.0f ) );
    const float horizonCos = -rho / radius;
    const float band       = Ref::SKY_TERMINATOR_HALF_WIDTH_COS;

    float previous = 0.0f;
    for ( int i = 0; i <= 200; ++i )
    {
        const float sunZenithCos = horizonCos - 2.0f * band + 4.0f * band * static_cast<float>( i ) / 200.0f;
        const float shadow       = Ref::SkyPlanetShadow( radius, sunZenithCos, p.BottomRadiusKm );

        EXPECT_GE( shadow, 0.0f );
        EXPECT_LE( shadow, 1.0f );
        EXPECT_GE( shadow, previous ) << "the fade must not dip as the sun rises";
        previous = shadow;
    }

    // Fully shadowed a band below the horizon, fully lit a band above it, and exactly half at it.
    EXPECT_FLOAT_EQ( Ref::SkyPlanetShadow( radius, horizonCos - band, p.BottomRadiusKm ), 0.0f );
    EXPECT_FLOAT_EQ( Ref::SkyPlanetShadow( radius, horizonCos + band, p.BottomRadiusKm ), 1.0f );
    EXPECT_NEAR( Ref::SkyPlanetShadow( radius, horizonCos, p.BottomRadiusKm ), 0.5f, 1e-5f );
}

// The band is narrower than the sun's own angular radius (~0.27 degrees, 0.0047 in cosine at the
// horizon). That direction matters: a band WIDER than the real penumbra would admit light the sky
// does not have and lift the whole twilight, which is the failure this fade must not become.
TEST( SkyScattering, TheTerminatorBandIsNoWiderThanTheSunItself )
{
    constexpr float kSunAngularRadiusCos = 0.00465f;
    EXPECT_LT( Ref::SKY_TERMINATOR_HALF_WIDTH_COS, kSunAngularRadiusCos );
    EXPECT_GT( Ref::SKY_TERMINATOR_HALF_WIDTH_COS, 0.0f );
}

// ---------------------------------------------------------------------------------------------------
// The integrator against a brute-force fine march
// ---------------------------------------------------------------------------------------------------

TEST( SkyScattering, IntegratorMatchesTheBruteForceMarch )
{
    ResetLutCallbacks();
    // Both sides read the SAME 200-step sun transmittance, so the comparison isolates what this test
    // is about — the integrator's 32-sample squared distribution and per-step analytic integral —
    // rather than re-testing the transmittance march (SkyMedium already pins 40 vs 20000 steps).
    Ref::g_SunTransmittance = []( Ref::SkyAtmParams p, float radiusKm, float sunZenithCos )
    { return Ref::SkyTransmittanceToTop( p, radiusKm, sunZenithCos, 200 ); };
    const Ref::SkyAtmParams p = EarthParams();

    const glm::vec3 illuminance( 1.0f );
    const glm::vec3 originKm( 0.0f, p.BottomRadiusKm + 0.5f, 0.0f );

    struct Case
    {
        glm::vec3   RayDir;
        glm::vec3   SunDir;
        const char* Name;
    };
    const Case cases[] = {
         { { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, "zenith ray, noon sun" },
         { { 0.0f, 1.0f, 0.0f }, glm::normalize( glm::vec3( 0.9f, 0.3f, 0.0f ) ), "zenith ray, low sun" },
         { glm::normalize( glm::vec3( 0.9f, 0.2f, 0.0f ) ), glm::normalize( glm::vec3( 0.7f, 0.6f, 0.2f ) ),
           "oblique ray, day sun" },
    };

    for ( const Case& c : cases )
    {
        // 32 samples: the shader's own Sky-View budget. The reference is 4000 uniform steps with a
        // 200-step sun transmittance per step — different distribution, different transmittance
        // source, same physics.
        const Ref::SkyScatterResult fast =
             Ref::SkyIntegrateScatteredLuminance( p, originKm, c.RayDir, c.SunDir, illuminance, 32 );
        const glm::vec3 fine = BruteForceSingleScattering( p, originKm, c.RayDir, c.SunDir, illuminance, 4000 );

        // 4% relative (plus an absolute floor for near-zero channels): the squared distribution vs
        // the uniform one, and the 40- vs 200-step sun transmittance.
        for ( int ch = 0; ch < 3; ++ch )
        {
            const float tolerance = std::max( 0.04f * fine[ch], 2e-5f );
            EXPECT_NEAR( fast.Luminance[ch], fine[ch], tolerance ) << c.Name << " channel " << ch;
        }
    }
}

TEST( SkyScattering, TransmittanceLaneMatchesTheMediumMarch )
{
    ResetLutCallbacks();
    const Ref::SkyAtmParams p = EarthParams();

    // For a non-ground ray the integrator's throughput IS the view-path transmittance — the same
    // quantity SkyMedium's dedicated integrator computes. Two implementations of one number.
    const glm::vec3 originKm( 0.0f, p.BottomRadiusKm + 0.5f, 0.0f );
    for ( const float mu : { 1.0f, 0.6f, 0.2f } )
    {
        const glm::vec3             rayDir( std::sqrt( 1.0f - mu * mu ), mu, 0.0f );
        const Ref::SkyScatterResult result = Ref::SkyIntegrateScatteredLuminance(
             p, originKm, rayDir, glm::vec3( 0.0f, 1.0f, 0.0f ), glm::vec3( 1.0f ), 64 );

        const glm::vec3 expected = Ref::SkyTransmittanceToTop( p, originKm.y, mu, 20000 );
        EXPECT_NEAR( result.Transmittance.x, expected.x, 0.01f ) << "mu=" << mu;
        EXPECT_NEAR( result.Transmittance.y, expected.y, 0.01f ) << "mu=" << mu;
        EXPECT_NEAR( result.Transmittance.z, expected.z, 0.01f ) << "mu=" << mu;
    }
}

// ---------------------------------------------------------------------------------------------------
// Energy sanity
// ---------------------------------------------------------------------------------------------------

TEST( SkyScattering, LuminanceIsNonNegativeAndTransmittanceIsAProbability )
{
    ResetLutCallbacks();
    const Ref::SkyAtmParams p = EarthParams();

    const glm::vec3 originKm( 0.0f, p.BottomRadiusKm + 0.2f, 0.0f );

    // The whole view sphere x sun elevations from noon to well below the horizon (twilight and night
    // must be DARK, never negative).
    for ( int si = 0; si <= 6; ++si )
    {
        const float     sunY = 1.0f - 2.0f * static_cast<float>( si ) / 6.0f;
        const float     sunX = std::sqrt( std::max( 1.0f - sunY * sunY, 0.0f ) );
        const glm::vec3 sunDir( sunX, sunY, 0.0f );

        for ( int zi = 0; zi <= 12; ++zi )
        {
            const float     mu = 1.0f - 2.0f * static_cast<float>( zi ) / 12.0f;
            const float     s  = std::sqrt( std::max( 1.0f - mu * mu, 0.0f ) );
            const glm::vec3 rayDir( s, mu, 0.0f );

            const Ref::SkyScatterResult result =
                 Ref::SkyIntegrateScatteredLuminance( p, originKm, rayDir, sunDir, glm::vec3( 10.0f ), 32 );

            for ( int ch = 0; ch < 3; ++ch )
            {
                EXPECT_GE( result.Luminance[ch], 0.0f ) << "sunY=" << sunY << " mu=" << mu;
                EXPECT_GT( result.Transmittance[ch], 0.0f ) << "sunY=" << sunY << " mu=" << mu;
                EXPECT_LE( result.Transmittance[ch], 1.0f ) << "sunY=" << sunY << " mu=" << mu;
            }
        }
    }
}

TEST( SkyScattering, PositiveAnisotropyMakesTheSunwardSkyBrighter )
{
    ResetLutCallbacks();
    const Ref::SkyAtmParams p = EarthParams(); // g = 0.8

    // Same sun, same elevation, opposite azimuths: the Mie forward lobe must make the ray TOWARD the
    // sun's azimuth brighter than the one away from it. A flipped phase convention passes every
    // normalization test and fails exactly this.
    const glm::vec3 originKm( 0.0f, p.BottomRadiusKm + 0.2f, 0.0f );
    const glm::vec3 sunDir = glm::normalize( glm::vec3( 0.9f, 0.44f, 0.0f ) );

    const glm::vec3 toward = glm::normalize( glm::vec3( 0.98f, 0.2f, 0.0f ) );
    const glm::vec3 away   = glm::normalize( glm::vec3( -0.98f, 0.2f, 0.0f ) );

    const Ref::SkyScatterResult sunward =
         Ref::SkyIntegrateScatteredLuminance( p, originKm, toward, sunDir, glm::vec3( 1.0f ), 32 );
    const Ref::SkyScatterResult antisolar =
         Ref::SkyIntegrateScatteredLuminance( p, originKm, away, sunDir, glm::vec3( 1.0f ), 32 );

    EXPECT_GT( sunward.Luminance.x + sunward.Luminance.y + sunward.Luminance.z,
               antisolar.Luminance.x + antisolar.Luminance.y + antisolar.Luminance.z );
}

TEST( SkyScattering, MultiScatterContributionIsLinearInPsi )
{
    ResetLutCallbacks();
    const Ref::SkyAtmParams p = EarthParams();

    const glm::vec3 originKm( 0.0f, p.BottomRadiusKm + 0.2f, 0.0f );
    const glm::vec3 rayDir( 0.0f, 1.0f, 0.0f );
    const glm::vec3 sunDir = glm::normalize( glm::vec3( 0.7f, 0.7f, 0.0f ) );

    const Ref::SkyScatterResult base =
         Ref::SkyIntegrateScatteredLuminance( p, originKm, rayDir, sunDir, glm::vec3( 1.0f ), 32 );

    Ref::g_MultiScatter = []( Ref::SkyAtmParams, float, float ) { return glm::vec3( 0.01f ); };
    const Ref::SkyScatterResult withPsi =
         Ref::SkyIntegrateScatteredLuminance( p, originKm, rayDir, sunDir, glm::vec3( 1.0f ), 32 );

    Ref::g_MultiScatter = []( Ref::SkyAtmParams, float, float ) { return glm::vec3( 0.02f ); };
    const Ref::SkyScatterResult withTwicePsi =
         Ref::SkyIntegrateScatteredLuminance( p, originKm, rayDir, sunDir, glm::vec3( 1.0f ), 32 );

    ResetLutCallbacks();

    // Additive, and linear: doubling Psi doubles exactly the delta it contributed. This is the
    // property that lets the MS LUT store a transfer rather than a luminance.
    const glm::vec3 delta1 = withPsi.Luminance - base.Luminance;
    const glm::vec3 delta2 = withTwicePsi.Luminance - base.Luminance;
    for ( int ch = 0; ch < 3; ++ch )
    {
        EXPECT_GT( delta1[ch], 0.0f );
        EXPECT_NEAR( delta2[ch], 2.0f * delta1[ch], 1e-5f + 0.001f * delta2[ch] );
    }

    // And Psi must not move the transmittance lane: it adds light, it does not remove any.
    EXPECT_FLOAT_EQ( base.Transmittance.x, withPsi.Transmittance.x );
}

// ---------------------------------------------------------------------------------------------------
// The sun disc
// ---------------------------------------------------------------------------------------------------

TEST( SkyScattering, SunDiscSoftEdgeIsOneInsideZeroOutside )
{
    const float apex    = 0.005f; // ~half-degree diameter
    const float cosApex = std::cos( apex );

    EXPECT_FLOAT_EQ( Ref::SkySunDiscSoftEdge( 1.0f, cosApex ), 1.0f );                    // centre
    EXPECT_FLOAT_EQ( Ref::SkySunDiscSoftEdge( cosApex, cosApex ), 0.0f );                 // the rim
    EXPECT_FLOAT_EQ( Ref::SkySunDiscSoftEdge( std::cos( 2.0f * apex ), cosApex ), 0.0f ); // outside

    // Monotone across the rim band.
    const float inner = Ref::SkySunDiscSoftEdge( std::cos( apex * 0.3f ), cosApex );
    const float outer = Ref::SkySunDiscSoftEdge( std::cos( apex * 0.8f ), cosApex );
    EXPECT_GT( inner, outer );
}

TEST( SkyScattering, LimbDarkeningFallsToARedderRim )
{
    // Centre: no darkening at all.
    const glm::vec3 centre = Ref::SkySunLimbDarkening( 0.0f );
    EXPECT_FLOAT_EQ( centre.x, 1.0f );
    EXPECT_FLOAT_EQ( centre.y, 1.0f );
    EXPECT_FLOAT_EQ( centre.z, 1.0f );

    // Strictly darkening outward, every channel.
    glm::vec3 previous = centre;
    for ( int i = 1; i <= 10; ++i )
    {
        const glm::vec3 sample = Ref::SkySunLimbDarkening( static_cast<float>( i ) / 10.0f );
        EXPECT_LT( sample.x, previous.x + 1e-6f );
        EXPECT_LT( sample.y, previous.y + 1e-6f );
        EXPECT_LT( sample.z, previous.z + 1e-6f );
        previous = sample;
    }

    // The rim is WARM: blue darkens fastest, red least — the ordering the 3-coefficient law encodes.
    const glm::vec3 nearRim = Ref::SkySunLimbDarkening( 0.95f );
    EXPECT_GT( nearRim.x, nearRim.y );
    EXPECT_GT( nearRim.y, nearRim.z );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
