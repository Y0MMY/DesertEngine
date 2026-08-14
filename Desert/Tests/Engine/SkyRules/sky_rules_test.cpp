// The sky's DECISIONS, tested without a GPU.
//
// The editor cannot run in this environment, so a sky rule that lives inside SkyboxRenderer can only be
// checked by looking at a picture — which is how a dead m_BakedSunDir sat in the renderer unread, and how
// an inverted sun shipped in four scenes. Everything asserted below is a pure function of numbers:
// Engine/Graphic/SkyRules.hpp, SkyPayload.hpp, AtmosphereEnv.hpp, SkySettings.hpp.

#include <Engine/Graphic/AtmosphereEnv.hpp>
#include <Engine/Graphic/ColorTemperature.hpp>
#include <Engine/Graphic/SkyPayload.hpp>
#include <Engine/Graphic/SkyRules.hpp>
#include <Engine/Graphic/SkySettings.hpp>

#include <Common/Core/Units.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>

using Desert::ECS::SkyAtmosphereData;
using Desert::ECS::SkyEnvironmentResolution;
using Desert::Graphic::AdvanceTimeOfDay;
using Desert::Graphic::BytesToMiB;
using Desert::Graphic::EnvironmentPanoramaSize;
using Desert::Graphic::EvaluateAtmosphere;
using Desert::Graphic::kSkyPackedVec4Count;
using Desert::Graphic::kSkyPayloadBytes;
using Desert::Graphic::kSkyRebakeMaxDeferSeconds;
using Desert::Graphic::kSkyRebakeSettleSeconds;
using Desert::Graphic::MakeSkySettings;
using Desert::Graphic::PackSky;
using Desert::Graphic::PlanetRadiusToWorldUnits;
using Desert::Graphic::ResolveSkyMode;
using Desert::Graphic::SelectPrimarySky;
using Desert::Graphic::ShouldRebakeSkyEnvironment;
using Desert::Graphic::SkyEnvironmentBakeCost;
using Desert::Graphic::SkyEnvironmentRebakeMayRun;
using Desert::Graphic::SkyGpuPayload;
using Desert::Graphic::SkyMode;
using Desert::Graphic::SkySettings;
using Desert::Graphic::SunDirectionFromTimeOfDay;

// ---------------------------------------------------------------------------------------------------
// Time of day -> sun direction. The return value is the direction the light TRAVELS (sun -> scene), so
// a sun overhead is a NEGATIVE y. Getting that backwards gives a world lit from underneath.
// ---------------------------------------------------------------------------------------------------

TEST( TimeOfDay, NoonOnTheEquatorPutsTheSunOverhead )
{
    const glm::vec3 travel = SunDirectionFromTimeOfDay( 12.0f, 0.0f, 0.0f );
    EXPECT_NEAR( travel.y, -1.0f, 1e-4f ) << "light travels DOWN when the sun is up";
    EXPECT_NEAR( glm::length( travel ), 1.0f, 1e-5f );
}

TEST( TimeOfDay, MidnightPutsTheSunUnderTheWorld )
{
    const glm::vec3 travel = SunDirectionFromTimeOfDay( 0.0f, 0.0f, 0.0f );
    EXPECT_NEAR( travel.y, 1.0f, 1e-4f );
}

TEST( TimeOfDay, SunriseAndSunsetSitOnTheHorizon )
{
    EXPECT_NEAR( SunDirectionFromTimeOfDay( 6.0f, 0.0f, 0.0f ).y, 0.0f, 1e-3f );
    EXPECT_NEAR( SunDirectionFromTimeOfDay( 18.0f, 0.0f, 0.0f ).y, 0.0f, 1e-3f );

    // ...and on OPPOSITE sides of the sky, which is the part a sign error would still pass without.
    const glm::vec3 dawn = SunDirectionFromTimeOfDay( 6.0f, 0.0f, 0.0f );
    const glm::vec3 dusk = SunDirectionFromTimeOfDay( 18.0f, 0.0f, 0.0f );
    EXPECT_NEAR( glm::dot( dawn, dusk ), -1.0f, 1e-3f );
}

TEST( TimeOfDay, LatitudeTiltsTheNoonSunTowardTheEquator )
{
    // Northern hemisphere: noon is 45 degrees up and to the SOUTH (-Z, since +Z is north).
    const glm::vec3 towardSun = -SunDirectionFromTimeOfDay( 12.0f, 45.0f, 0.0f );
    EXPECT_NEAR( towardSun.y, std::cos( glm::radians( 45.0f ) ), 1e-4f );
    EXPECT_NEAR( towardSun.z, -std::sin( glm::radians( 45.0f ) ), 1e-4f );
    EXPECT_NEAR( towardSun.x, 0.0f, 1e-4f );

    // Southern hemisphere mirrors it to the north; the elevation is the same.
    const glm::vec3 south = -SunDirectionFromTimeOfDay( 12.0f, -45.0f, 0.0f );
    EXPECT_NEAR( south.y, towardSun.y, 1e-4f );
    EXPECT_NEAR( south.z, -towardSun.z, 1e-4f );
}

TEST( TimeOfDay, NorthOffsetRotatesTheAzimuthAboutWorldY )
{
    // Measured at latitude 45, because at the equator noon is straight up and its azimuth is undefined —
    // a test written there would pass for a function that ignored NorthOffset entirely.
    const glm::vec3 before = -SunDirectionFromTimeOfDay( 12.0f, 45.0f, 0.0f );
    const glm::vec3 after  = -SunDirectionFromTimeOfDay( 12.0f, 45.0f, 90.0f );

    EXPECT_NEAR( after.y, before.y, 1e-4f ) << "a rotation about Y cannot change the elevation";

    const glm::vec2 h0 = glm::normalize( glm::vec2( before.x, before.z ) );
    const glm::vec2 h1 = glm::normalize( glm::vec2( after.x, after.z ) );
    EXPECT_NEAR( glm::dot( h0, h1 ), 0.0f, 1e-4f ) << "90 degrees apart on the ground plane";
}

TEST( TimeOfDay, ClockAdvancesWrapsAndFreezes )
{
    // A 600-second day: one real second is 0.04 h.
    EXPECT_NEAR( AdvanceTimeOfDay( 12.0f, 1.0f, 600.0f ), 12.04f, 1e-4f );

    // Wraps at 24 rather than running off to 25.
    EXPECT_NEAR( AdvanceTimeOfDay( 23.99f, 1.0f, 600.0f ), 0.03f, 1e-3f );

    // DayLengthSeconds == 0 freezes the sun at the authored hour — that is what makes the feature usable
    // as a posing tool and not only as an animation.
    EXPECT_FLOAT_EQ( AdvanceTimeOfDay( 7.5f, 1.0f, 0.0f ), 7.5f );
    EXPECT_FLOAT_EQ( AdvanceTimeOfDay( 7.5f, 1000.0f, 0.0f ), 7.5f );
}

// ---------------------------------------------------------------------------------------------------
// IBL rebake throttle
// ---------------------------------------------------------------------------------------------------

namespace
{
    // A toward-sun direction @p degrees away from straight up, in the XY plane.
    glm::vec3 SunAt( float degrees )
    {
        const float r = glm::radians( degrees );
        return glm::vec3( std::sin( r ), std::cos( r ), 0.0f );
    }
} // namespace

TEST( Rebake, ExplicitRequestAlwaysBakes )
{
    for ( const bool autoRebake : { true, false } )
        for ( const bool hasEnv : { true, false } )
            EXPECT_TRUE( ShouldRebakeSkyEnvironment( SunAt( 0.0f ), SunAt( 0.0f ), 5.0f, autoRebake, hasEnv,
                                                     /*explicitRequest=*/true ) );
}

TEST( Rebake, FirstBakeHappensEvenWithAutoRebakeOff )
{
    // Without this the scene has no ambient light at all. "Auto Rebake off" is a request to stop
    // RE-baking, not a request to render an unlit world.
    EXPECT_TRUE( ShouldRebakeSkyEnvironment( SunAt( 0.0f ), SunAt( 0.0f ), 5.0f, /*autoRebake=*/false,
                                             /*hasEnvironment=*/false, false ) );
}

TEST( RebakeDebounce, ADragCollapsesIntoOneBakeWhenItEnds )
{
    // The angular threshold says the environment is STALE; this rule says when to act on it. At 5 degrees
    // a drag crosses the threshold several times a second, and every crossing used to idle the device and
    // rebuild four cube images. Nothing runs while the sun is still moving...
    for ( float held = 0.0f; held < kSkyRebakeSettleSeconds; held += 0.01f )
        EXPECT_FALSE( SkyEnvironmentRebakeMayRun( held, /*secondsSinceStale=*/0.05f, kSkyRebakeSettleSeconds,
                                                  kSkyRebakeMaxDeferSeconds ) )
             << "sun still for " << held << " s";

    // ...and exactly once it stops.
    EXPECT_TRUE( SkyEnvironmentRebakeMayRun( kSkyRebakeSettleSeconds, 0.05f, kSkyRebakeSettleSeconds,
                                             kSkyRebakeMaxDeferSeconds ) );
}

TEST( RebakeDebounce, ASunThatNeverStopsStillGetsBaked )
{
    // The trap this bound exists for: the time-of-day driver moves the sun EVERY frame, so "wait until it
    // holds still" alone would defer the bake forever and freeze the environment at whatever hour the
    // scene was opened at. Sun permanently in motion, and it still refreshes.
    EXPECT_FALSE( SkyEnvironmentRebakeMayRun( 0.0f, kSkyRebakeMaxDeferSeconds * 0.5f, kSkyRebakeSettleSeconds,
                                              kSkyRebakeMaxDeferSeconds ) );
    EXPECT_TRUE( SkyEnvironmentRebakeMayRun( 0.0f, kSkyRebakeMaxDeferSeconds, kSkyRebakeSettleSeconds,
                                             kSkyRebakeMaxDeferSeconds ) );
}

TEST( RebakeDebounce, EitherConditionIsEnoughAndNeitherGoesBackwards )
{
    // Monotone in both arguments: waiting longer never turns a bake back off, which is what keeps the
    // deferral from oscillating at the boundary.
    for ( float still = 0.0f; still <= 0.30f; still += 0.01f )
    {
        bool seenTrue = false;
        for ( float stale = 0.0f; stale <= 2.0f; stale += 0.05f )
        {
            const bool may =
                 SkyEnvironmentRebakeMayRun( still, stale, kSkyRebakeSettleSeconds, kSkyRebakeMaxDeferSeconds );
            if ( seenTrue )
                EXPECT_TRUE( may ) << "still " << still << " stale " << stale;
            seenTrue = seenTrue || may;
        }
    }
}

TEST( Rebake, ThresholdIsHonouredOnBothSides )
{
    EXPECT_FALSE( ShouldRebakeSkyEnvironment( SunAt( 0.0f ), SunAt( 4.9f ), 5.0f, true, true, false ) );
    EXPECT_TRUE( ShouldRebakeSkyEnvironment( SunAt( 0.0f ), SunAt( 5.1f ), 5.0f, true, true, false ) );
}

TEST( Rebake, AutoRebakeOffSuppressesSunMovement )
{
    for ( const float move : { 1.0f, 45.0f, 179.0f } )
        EXPECT_FALSE( ShouldRebakeSkyEnvironment( SunAt( 0.0f ), SunAt( move ), 5.0f, /*autoRebake=*/false,
                                                  /*hasEnvironment=*/true, false ) );
}

TEST( Rebake, AntipodalSunsDoNotProduceNaN )
{
    const glm::vec3 up   = glm::vec3( 0.0f, 1.0f, 0.0f );
    const glm::vec3 down = -up;

    // acos() of a dot product that lands on -1 - 1e-7 in float is NaN, and NaN compares false against
    // every threshold — which would silently disable rebaking forever rather than loudly break.
    EXPECT_TRUE( ShouldRebakeSkyEnvironment( up, down, 5.0f, true, true, false ) );
    EXPECT_TRUE( ShouldRebakeSkyEnvironment( up * 3.0f, down * 7.0f, 5.0f, true, true, false ) );
    EXPECT_FALSE( ShouldRebakeSkyEnvironment( up, up, 5.0f, true, true, false ) );
}

// ---------------------------------------------------------------------------------------------------
// Sky mode + duplicate skies
// ---------------------------------------------------------------------------------------------------

TEST( SkyModeRule, CoversAllFourCombinations )
{
    EXPECT_EQ( ResolveSkyMode( true, true ), SkyMode::Atmosphere ) << "the atmosphere wins the pass";
    EXPECT_EQ( ResolveSkyMode( true, false ), SkyMode::Atmosphere );
    EXPECT_EQ( ResolveSkyMode( false, true ), SkyMode::HdrCubemap );
    EXPECT_EQ( ResolveSkyMode( false, false ), SkyMode::None );
}

TEST( PrimarySky, PicksTheLowestIdAndIsOrderIndependent )
{
    const std::array<uint64_t, 3> a{ 90u, 12u, 55u };
    const std::array<uint64_t, 3> b{ 55u, 90u, 12u };

    ASSERT_TRUE( SelectPrimarySky( a ).has_value() );
    ASSERT_TRUE( SelectPrimarySky( b ).has_value() );
    EXPECT_EQ( a[*SelectPrimarySky( a )], 12u );
    EXPECT_EQ( b[*SelectPrimarySky( b )], 12u ) << "shuffling the scene must not change the sky";

    EXPECT_FALSE( SelectPrimarySky( std::span<const uint64_t>{} ).has_value() );
}

// ---------------------------------------------------------------------------------------------------
// Environment resolution and what it costs
// ---------------------------------------------------------------------------------------------------

TEST( EnvironmentResolution, LadderIsWorkGroupAligned )
{
    for ( const auto res :
          { SkyEnvironmentResolution::Low, SkyEnvironmentResolution::Medium, SkyEnvironmentResolution::High } )
    {
        const auto size = EnvironmentPanoramaSize( res );
        // The bake dispatches in 32x32 groups and the shader has no partial-group path, so a size that is
        // not a multiple of 32 leaves the panorama's right/bottom edge unwritten.
        EXPECT_EQ( size.Width % 32u, 0u );
        EXPECT_EQ( size.Height % 32u, 0u );
        EXPECT_EQ( size.Width, size.Height * 2u ) << "equirect panoramas are 2:1";
    }

    EXPECT_EQ( EnvironmentPanoramaSize( SkyEnvironmentResolution::Medium ).Width, 1024u )
         << "Medium must stay what the engine baked at unconditionally, or every scene changes look";
}

TEST( EnvironmentResolution, PanoramaCostIsTheAdvertisedNumber )
{
    // RGBA32F is 16 B/px: 2048*1024*16 = 32 MiB, 1024*512*16 = 8 MiB.
    EXPECT_NEAR( BytesToMiB( SkyEnvironmentBakeCost( SkyEnvironmentResolution::High ).PanoramaBytes ), 32.0,
                 1e-6 );
    EXPECT_NEAR( BytesToMiB( SkyEnvironmentBakeCost( SkyEnvironmentResolution::Medium ).PanoramaBytes ), 8.0,
                 1e-6 );
    EXPECT_NEAR( BytesToMiB( SkyEnvironmentBakeCost( SkyEnvironmentResolution::Low ).PanoramaBytes ), 2.0, 1e-6 );

    // The cube chain does NOT scale with the ladder — it is a fixed 1024-texel face either way. That is
    // the fact the log line exists to tell you, so it had better be true.
    const auto high = SkyEnvironmentBakeCost( SkyEnvironmentResolution::High );
    const auto low  = SkyEnvironmentBakeCost( SkyEnvironmentResolution::Low );
    EXPECT_EQ( high.CubeBytes, low.CubeBytes );
    EXPECT_EQ( high.TotalBytes, high.PanoramaBytes + high.CubeBytes );
}

TEST( PlanetRadius, KilometresBecomeCentimetres )
{
    // 1 world unit = 1 cm, so the default 6360 km is 6.36e8 units.
    EXPECT_FLOAT_EQ( PlanetRadiusToWorldUnits( 6360.0f ), 636000000.0f );
    EXPECT_FLOAT_EQ( PlanetRadiusToWorldUnits( 1.0f ), Common::Units::Metres( 1000.0f ) );
}

// ---------------------------------------------------------------------------------------------------
// Component -> transport -> GPU payload
// ---------------------------------------------------------------------------------------------------

TEST( MakeSkySettingsRule, ConvertsTheArtistsUnitsIntoTheRenderers )
{
    SkyAtmosphereData data;
    data.SunAngularDiameter = 2.2918f; // degrees, DIAMETER
    data.PlanetRadius       = 6360.0f; // kilometres

    const SkySettings sky = MakeSkySettings( data );

    // Degrees -> radians AND diameter -> radius, both exactly once.
    EXPECT_NEAR( sky.SunAngularRadius, glm::radians( 2.2918f ) * 0.5f, 1e-7f );
    EXPECT_NEAR( glm::degrees( sky.SunAngularRadius ) * 2.0f, 2.2918f, 1e-4f );
    EXPECT_FLOAT_EQ( sky.PlanetRadius, 636000000.0f );

    // The palette and the bake knobs come across untouched.
    EXPECT_EQ( sky.ZenithColor, data.ZenithColor );
    EXPECT_EQ( sky.NightColor, data.NightColor );
    EXPECT_FLOAT_EQ( sky.StarIntensity, data.StarIntensity );
    EXPECT_EQ( sky.EnvironmentResolution, data.EnvironmentResolution );
    EXPECT_FLOAT_EQ( sky.RebakeSunAngleThreshold, data.RebakeSunAngleThreshold );
}

TEST( SkyPayloadLayout, EveryAuthoredValueLandsWhereTheShaderReadsIt )
{
    // The shader reads this block through Common/Atmosphere.glslh's unpack helpers, so the assertions
    // below ARE the shader's view of it: v[0].w is the sun intensity, v[6].w is the angular radius, and
    // so on. A member inserted in the middle fails here instead of corrupting the frame.
    // 13 lanes since the physical-atmosphere medium block (v[7]-v[12]) was APPENDED for the LUT passes.
    EXPECT_EQ( sizeof( SkyGpuPayload ), kSkyPackedVec4Count * sizeof( glm::vec4 ) );
    EXPECT_EQ( kSkyPayloadBytes, 13u * 16u );

    SkyAtmosphereData data;
    data.ZenithColor        = { 0.1f, 0.2f, 0.3f };
    data.HorizonColor       = { 0.4f, 0.5f, 0.6f };
    data.SunColor           = { 0.7f, 0.8f, 0.9f };
    data.SunsetColor        = { 0.11f, 0.12f, 0.13f };
    data.GroundColor        = { 0.14f, 0.15f, 0.16f };
    data.NightColor         = { 0.17f, 0.18f, 0.19f };
    data.SkyBrightness      = 1.25f;
    data.HorizonFalloff     = 0.65f;
    data.SunGlow            = 2.5f;
    data.SunsetIntensity    = 1.75f;
    data.StarIntensity      = 3.25f;
    data.SunIntensity       = 17.0f;
    data.SunAngularDiameter = 4.0f;

    const glm::vec3     toward = glm::normalize( glm::vec3( 0.3f, 0.9f, 0.3f ) );
    const SkyGpuPayload p      = PackSky( toward, MakeSkySettings( data ) );

    const auto* lanes = reinterpret_cast<const glm::vec4*>( &p );

    EXPECT_EQ( glm::vec3( lanes[0] ), toward );
    EXPECT_FLOAT_EQ( lanes[0].w, 17.0f );

    EXPECT_EQ( glm::vec3( lanes[1] ), data.ZenithColor );
    EXPECT_FLOAT_EQ( lanes[1].w, 1.25f );
    EXPECT_EQ( glm::vec3( lanes[2] ), data.HorizonColor );
    EXPECT_FLOAT_EQ( lanes[2].w, 0.65f );
    EXPECT_EQ( glm::vec3( lanes[3] ), data.SunColor );
    EXPECT_FLOAT_EQ( lanes[3].w, 2.5f );
    EXPECT_EQ( glm::vec3( lanes[4] ), data.SunsetColor );
    EXPECT_FLOAT_EQ( lanes[4].w, 1.75f );
    EXPECT_EQ( glm::vec3( lanes[5] ), data.GroundColor );
    EXPECT_FLOAT_EQ( lanes[5].w, 3.25f );
    EXPECT_EQ( glm::vec3( lanes[6] ), data.NightColor );
    EXPECT_NEAR( lanes[6].w, glm::radians( 4.0f ) * 0.5f, 1e-7f ) << "RADIANS, and a radius";

    // The medium block, exactly where SkyMedium.glslh's SkyMakeAtmParams expects each lane. The
    // coefficients arrive as MakeSkySettings' scale x colour PRODUCTS (per kilometre), and the planet
    // radius arrives in WORLD UNITS — the km conversion belongs to the shader, and only to it.
    EXPECT_EQ( glm::vec3( lanes[7] ),
               data.RayleighScatteringScale * data.RayleighScattering ); // 0.0331 x colour, /km
    EXPECT_FLOAT_EQ( lanes[7].w, data.RayleighExponentialDistribution );
    EXPECT_EQ( glm::vec3( lanes[8] ), data.MieScatteringScale * data.MieScattering );
    EXPECT_FLOAT_EQ( lanes[8].w, data.MieExponentialDistribution );
    EXPECT_EQ( glm::vec3( lanes[9] ), data.MieAbsorptionScale * data.MieAbsorption );
    EXPECT_FLOAT_EQ( lanes[9].w, data.MieAnisotropy );
    EXPECT_EQ( glm::vec3( lanes[10] ), data.OtherAbsorptionScale * data.OtherAbsorption );
    EXPECT_FLOAT_EQ( lanes[10].w, data.AtmosphereHeight );
    EXPECT_EQ( glm::vec3( lanes[11] ), data.GroundAlbedo );
    EXPECT_FLOAT_EQ( lanes[11].w, data.MultiScatteringFactor );
    EXPECT_FLOAT_EQ( lanes[12].x, data.AbsorptionTipAltitude );
    EXPECT_FLOAT_EQ( lanes[12].y, data.AbsorptionTipValue );
    EXPECT_FLOAT_EQ( lanes[12].z, data.AbsorptionTentWidth );
    EXPECT_FLOAT_EQ( lanes[12].w, PlanetRadiusToWorldUnits( data.PlanetRadius ) );
}

// ---------------------------------------------------------------------------------------------------
// The evaluated state the cloud pass consumes
// ---------------------------------------------------------------------------------------------------

TEST( AtmosphereEnvRule, NightFactorMatchesTheShadersDayBlend )
{
    SkySettings sky;

    // Atmosphere.glslh: day = smoothstep(-0.10, 0.20, sunDir.y); night = 1 - day. Hand-computed here so a
    // change to either side has to be a deliberate change to both.
    struct Case
    {
        float SunY;
        float ExpectedNight;
    };
    const Case cases[] = { { -0.20f, 1.0f }, // below the smoothstep's low edge -> full night
                           { -0.10f, 1.0f }, // exactly the low edge
                           { 0.05f, 0.5f },  // the midpoint: smoothstep(t=0.5) = 0.5
                           { 0.20f, 0.0f },  // the high edge -> full day
                           { 0.90f, 0.0f } };

    for ( const auto& c : cases )
    {
        const float     horizontal = std::sqrt( std::max( 0.0f, 1.0f - c.SunY * c.SunY ) );
        const glm::vec3 toward( horizontal, c.SunY, 0.0f );
        const auto      env = EvaluateAtmosphere( sky, toward, nullptr );
        EXPECT_NEAR( env.NightFactor, c.ExpectedNight, 1e-5f ) << "sun y = " << c.SunY;
    }
}

TEST( AtmosphereEnvRule, AmbientIsTheDomeNotTheZenithTexel )
{
    // CLD-100/101. The sky ambient a cloud receives is the hemisphere it hangs against, blended toward
    // the horizon colour by solid angle — the zenith texel alone is the dome's darkest, bluest corner
    // and feeding it to the clouds painted every shadowed face navy. The ground term is what the ground
    // REFLECTS (sun + dome, times albedo over pi), not the tone the ground is painted with.
    SkySettings sky;
    sky.ZenithColor   = { 0.08f, 0.26f, 0.70f };
    sky.HorizonColor  = { 0.50f, 0.66f, 0.92f };
    sky.NightColor    = { 0.01f, 0.02f, 0.05f };
    sky.GroundColor   = { 0.16f, 0.19f, 0.24f };
    sky.SkyBrightness = 2.0f;

    const auto day = EvaluateAtmosphere( sky, glm::vec3( 0.0f, 1.0f, 0.0f ), nullptr );

    const glm::vec3 dome = glm::mix( sky.ZenithColor, sky.HorizonColor, 0.65f ) * sky.SkyBrightness;
    EXPECT_NEAR( day.ZenithRadiance.r, dome.r, 1e-5f ) << "dome blend, scaled by Sky Brightness";
    EXPECT_NEAR( day.ZenithRadiance.b, dome.b, 1e-5f );
    EXPECT_GT( day.ZenithRadiance.r / day.ZenithRadiance.b, 0.08f / 0.70f )
         << "the dome must be less blue than the zenith texel alone";

    const glm::vec3 expectedGround =
         sky.GroundColor * ( day.SunIrradiance * 1.0f + day.ZenithRadiance ) * 0.3183099f;
    EXPECT_NEAR( day.GroundRadiance.r, expectedGround.r, 1e-4f )
         << "ground bounce reflects sun + dome, Lambertian";
    EXPECT_NEAR( day.GroundRadiance.b, expectedGround.b, 1e-4f );

    const auto night = EvaluateAtmosphere( sky, glm::vec3( 0.0f, -1.0f, 0.0f ), nullptr );
    EXPECT_NEAR( night.ZenithRadiance.b, 0.05f * 2.0f, 1e-5f ) << "night: the dome resolves to the night colour";

    const glm::vec3 expectedNightGround = 0.30f * sky.GroundColor * night.ZenithRadiance * 0.3183099f;
    EXPECT_NEAR( night.GroundRadiance.b, expectedNightGround.b, 1e-5f )
         << "at night the ground reflects only the night sky";
}

TEST( AtmosphereEnvRule, TheSunTakesTheSunsetColourAsItGoesDown )
{
    // SunIrradiance is what the cloud march is lit by, and it is the ONLY route by which the colour of
    // the light reaches a cloud. It used to be SunColor * SunIntensity with no dependence on elevation,
    // so the sky reddened at dusk and every cloud in it stayed noon-white. The tint now mirrors
    // Atmosphere.glslh's own — mix(sunsetColor, sunColor, smoothstep(0, 0.25, sunUp)) — which is what
    // makes "the sky and the clouds see one sun" a fact rather than a comment.
    SkyAtmosphereData data;
    data.SunColor     = { 1.0f, 1.0f, 1.0f }; // deliberately neutral: any warmth must come from the ramp
    data.SunsetColor  = { 1.0f, 0.4f, 0.2f };
    data.SunIntensity = 10.0f;

    const SkySettings sky = MakeSkySettings( data );

    // High: the sun's own colour, untouched.
    const auto high = EvaluateAtmosphere( sky, glm::vec3( 0.0f, 1.0f, 0.0f ), nullptr );
    EXPECT_NEAR( high.SunIrradiance.r / high.SunIrradiance.b, 1.0f, 1e-5f );

    // On the horizon: the sunset colour, and therefore red-dominant.
    const auto low = EvaluateAtmosphere( sky, glm::vec3( 1.0f, 0.0f, 0.0f ), nullptr );
    EXPECT_GT( low.SunIrradiance.r / low.SunIrradiance.b, 4.0f );

    // And it reddens MONOTONICALLY on the way down — no band that jumps.
    float previousRatio = 0.0f;
    for ( float y = 0.30f; y >= 0.0f; y -= 0.02f )
    {
        const auto  env   = EvaluateAtmosphere( sky, glm::vec3( 1.0f, y, 0.0f ), nullptr );
        const float ratio = env.SunIrradiance.r / std::max( env.SunIrradiance.b, 1e-6f );
        EXPECT_GE( ratio, previousRatio - 1e-5f ) << "sun y = " << y;
        previousRatio = ratio;
    }
}

TEST( AtmosphereEnvRule, TheSunStopsLightingCloudsOnceItIsDown )
{
    // NightFactor was computed here and consumed by nothing, so a cloud at midnight went on receiving the
    // full noon irradiance while the sky behind it had gone dark. What lights a cloud after sunset is the
    // night sky, and that arrives through ZenithRadiance instead.
    SkyAtmosphereData data;
    data.SunColor     = { 1.0f, 1.0f, 1.0f };
    data.SunIntensity = 10.0f;

    const SkySettings sky   = MakeSkySettings( data );
    const auto        night = EvaluateAtmosphere( sky, glm::vec3( 0.0f, -1.0f, 0.0f ), nullptr );

    EXPECT_FLOAT_EQ( night.NightFactor, 1.0f );
    EXPECT_NEAR( night.SunIrradiance.r, 0.0f, 1e-6f );
    EXPECT_NEAR( night.SunIrradiance.g, 0.0f, 1e-6f );
    EXPECT_NEAR( night.SunIrradiance.b, 0.0f, 1e-6f );

    // Never negative on the way there, and never brighter than the daylight value.
    const auto noon = EvaluateAtmosphere( sky, glm::vec3( 0.0f, 1.0f, 0.0f ), nullptr );
    for ( float y = 1.0f; y >= -1.0f; y -= 0.05f )
    {
        const auto env = EvaluateAtmosphere( sky, glm::vec3( 0.3f, y, 0.0f ), nullptr );
        EXPECT_GE( env.SunIrradiance.r, 0.0f ) << "sun y = " << y;
        EXPECT_LE( env.SunIrradiance.r, noon.SunIrradiance.r + 1e-5f ) << "sun y = " << y;
    }
}

TEST( AtmosphereEnvRule, CarriesTheSunAndThePlanetAndNormalizes )
{
    SkyAtmosphereData data;
    data.SunColor           = { 1.0f, 0.5f, 0.25f };
    data.SunIntensity       = 4.0f;
    data.SunAngularDiameter = 2.2918f;
    data.PlanetRadius       = 6360.0f;

    const auto env = EvaluateAtmosphere( MakeSkySettings( data ),
                                         glm::vec3( 0.0f, 5.0f, 0.0f ) /*deliberately un-normalized*/, nullptr );

    EXPECT_NEAR( glm::length( env.SunDirection ), 1.0f, 1e-6f );
    EXPECT_FLOAT_EQ( env.SunIrradiance.r, 4.0f );
    EXPECT_FLOAT_EQ( env.SunIrradiance.g, 2.0f );
    EXPECT_NEAR( env.SunAngularRadius, glm::radians( 2.2918f ) * 0.5f, 1e-7f );
    EXPECT_FLOAT_EQ( env.PlanetRadius, 636000000.0f );
    EXPECT_TRUE( env.Valid );
    EXPECT_EQ( env.ParamsBuffer, nullptr );
}

TEST( AtmosphereEnvRule, DefaultConstructedStateIsInvalidAndCarriesNoBuffer )
{
    // What the renderer publishes when no enabled sky drives the frame. A consumer that drew anyway would
    // be drawing against the previous frame's sun, so both facts have to hold together.
    const Desert::Graphic::AtmosphereEnv env;
    EXPECT_FALSE( env.Valid );
    EXPECT_EQ( env.ParamsBuffer, nullptr );
}

TEST( ColorTemperature, MatchesUnrealsConversionAndBehavesPhysically )
{
    using Desert::Graphic::ColorFromTemperature;

    // 6500 K is the D65-adjacent illuminant: every channel lands near 1 (within ten percent) without
    // being exactly white — the property that matters, pinned instead of a secondhand sample value.
    // The formula itself is transcribed verbatim from FLinearColor::MakeFromColorTemperature (Krystek
    // 1985 -> xyY -> XYZ -> linear BT.709), constants and all.
    const glm::vec3 d65 = ColorFromTemperature( 6500.0f );
    EXPECT_NEAR( d65.r, 1.0f, 0.1f );
    EXPECT_NEAR( d65.g, 1.0f, 0.1f );
    EXPECT_NEAR( d65.b, 1.0f, 0.1f );
    EXPECT_GT( d65.r, d65.g ) << "slightly warm of pure white, as the locus is at 6500 K";

    // A candle is red-dominant, a clear-sky blue is blue-dominant, and the red:blue ratio falls
    // MONOTONICALLY with temperature — the property a hue slider is trusted for.
    EXPECT_GT( ColorFromTemperature( 1800.0f ).r, ColorFromTemperature( 1800.0f ).b * 3.0f );
    EXPECT_GT( ColorFromTemperature( 12000.0f ).b, ColorFromTemperature( 12000.0f ).r );

    float previous = 1e9f;
    for ( float k = 1000.0f; k <= 15000.0f; k += 250.0f )
    {
        const glm::vec3 c     = ColorFromTemperature( k );
        const float     ratio = c.r / glm::max( c.b, 1e-4f );
        EXPECT_LE( ratio, previous + 1e-4f ) << "kelvin = " << k;
        previous = ratio;
    }

    // The conversion clamps to its published domain rather than extrapolating the fit.
    EXPECT_EQ( ColorFromTemperature( 100.0f ), ColorFromTemperature( 1000.0f ) );
    EXPECT_EQ( ColorFromTemperature( 50000.0f ), ColorFromTemperature( 15000.0f ) );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
