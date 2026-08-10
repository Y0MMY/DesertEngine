// The sky's DECISIONS, tested without a GPU.
//
// The editor cannot run in this environment, so a sky rule that lives inside SkyboxRenderer can only be
// checked by looking at a picture — which is how a dead m_BakedSunDir sat in the renderer unread, and how
// an inverted sun shipped in four scenes. Everything asserted below is a pure function of numbers:
// Engine/Graphic/SkyRules.hpp, SkyPayload.hpp, AtmosphereEnv.hpp, SkySettings.hpp.

#include <Engine/Graphic/AtmosphereEnv.hpp>
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
using Desert::Graphic::MakeSkySettings;
using Desert::Graphic::PackSky;
using Desert::Graphic::PlanetRadiusToWorldUnits;
using Desert::Graphic::ResolveSkyMode;
using Desert::Graphic::SelectPrimarySky;
using Desert::Graphic::ShouldRebakeSkyEnvironment;
using Desert::Graphic::SkyEnvironmentBakeCost;
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
    EXPECT_EQ( sizeof( SkyGpuPayload ), kSkyPackedVec4Count * sizeof( glm::vec4 ) );
    EXPECT_EQ( kSkyPayloadBytes, 7u * 16u );

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

TEST( AtmosphereEnvRule, AmbientFollowsTheSameGradientTheShaderDraws )
{
    SkySettings sky;
    sky.ZenithColor   = { 0.08f, 0.26f, 0.70f };
    sky.NightColor    = { 0.01f, 0.02f, 0.05f };
    sky.GroundColor   = { 0.16f, 0.19f, 0.24f };
    sky.SkyBrightness = 2.0f;

    const auto day = EvaluateAtmosphere( sky, glm::vec3( 0.0f, 1.0f, 0.0f ), nullptr );
    EXPECT_NEAR( day.ZenithRadiance.b, 0.70f * 2.0f, 1e-5f ) << "zenith is scaled by Sky Brightness";
    EXPECT_NEAR( day.GroundRadiance.b, 0.24f, 1e-5f )
         << "the shader mixes the ground in AFTER the brightness multiply, so it must not be scaled";

    const auto night = EvaluateAtmosphere( sky, glm::vec3( 0.0f, -1.0f, 0.0f ), nullptr );
    EXPECT_NEAR( night.ZenithRadiance.b, 0.05f * 2.0f, 1e-5f ) << "night tint at the zenith";
    EXPECT_NEAR( night.GroundRadiance.b, 0.24f * 0.30f, 1e-5f );
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

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
