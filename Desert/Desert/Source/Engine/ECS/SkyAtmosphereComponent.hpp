#pragma once

#include <cstdint>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>

#include <Engine/Reflection/ReflectionMacros.hpp>

namespace Desert::ECS
{
    // Size of the equirectangular panorama the sky is baked into for image-based lighting. The bake is a
    // WaitDeviceIdle operation and its cost is paid PER LIVE SceneRenderer (viewport, mesh preview,
    // thumbnail renderer, extra scene views), which is why the size is authored and not a constant.
    enum class SkyEnvironmentResolution : uint8_t
    {
        Low,    // 512x256
        Medium, // 1024x512 - what the engine baked at unconditionally before this was authorable
        High    // 2048x1024
    };

    // Which preset the palette last came from. An ENUM rather than a string on purpose: a string can hold
    // a preset that does not exist (a typo, a renamed preset, a hand-edited scene file) and there is no
    // validation layer between the file and the widget that would catch it. Removing a preset is then a
    // build error instead of a load-time surprise.
    enum class SkyPreset : uint8_t
    {
        Custom, // not a preset: "these values were authored by hand"
        ClearNoon,
        GoldenHour,
        OvercastGrey,
        Night,
        StudioNeutral
    };

    // The procedural atmosphere: the sky the engine generates when no HDR cubemap is used. Everything the
    // sky pass, the IBL bake and the time-of-day driver read lives here; SkyboxComponent keeps only the HDR
    // path (its asset handle and intensity).
    //
    // ON THE TWO PAIRS OF SUN NUMBERS. `SunColor` x `SunIntensity` here and `Color` x `Intensity` on
    // DirectionalLightData are NOT the same quantity stored twice:
    //   * these two are the RADIANCE of the sky and of the solar disk - how bright the sun looks IN THE
    //     PICTURE. They are written into the sky's parameter block and into the IBL bake.
    //   * the light's two are the ILLUMINANCE arriving at scene surfaces - how bright the sun is ON THE
    //     GROUND. They are written into DirectionLightsUB and integrated by every PBR surface.
    // No code path reads one where it means the other, and neither is derived from the other. The one
    // quantity they genuinely share is DIRECTION, and that has a single owner: the atmosphere sun light's
    // TransformComponent (see DirectionalLightData::AtmosphereSunLight).
    struct SkyAtmosphereData
    {
        REFLECT()

        PROPERTY( DisplayName( "Enabled" ), Category( "Atmosphere" ), Summary )
        bool Enabled = true;

        PROPERTY( DisplayName( "Sky Brightness" ), Category( "Atmosphere" ), Range( 0.0f, 4.0f ), Units( "x" ) )
        float SkyBrightness = 1.0f;

        PROPERTY( DisplayName( "Horizon Falloff" ), Category( "Atmosphere" ), Range( 0.1f, 2.0f ) )
        float HorizonFalloff = 0.85f;

        PROPERTY( DisplayName( "Zenith Color" ), Category( "Sky Color" ), Color )
        glm::vec3 ZenithColor = { 0.08f, 0.26f, 0.70f };

        PROPERTY( DisplayName( "Horizon Color" ), Category( "Sky Color" ), Color )
        glm::vec3 HorizonColor = { 0.50f, 0.66f, 0.92f };

        PROPERTY( DisplayName( "Ground Color" ), Category( "Sky Color" ), Color )
        glm::vec3 GroundColor = { 0.16f, 0.19f, 0.24f };

        PROPERTY( DisplayName( "Night Color" ), Category( "Sky Color" ), Color )
        glm::vec3 NightColor = { 0.010f, 0.020f, 0.050f };

        PROPERTY( DisplayName( "Sun Intensity" ), Category( "Sun" ), Range( 1.0f, 50.0f ), Units( "x" ), Summary,
                  Tooltip( "Brightness of the sky and the solar disk as seen by the camera. Scene surfaces "
                           "are lit by the directional light's own Colour and Intensity, not by this." ) )
        float SunIntensity = 22.0f;

        PROPERTY( DisplayName( "Sun Color" ), Category( "Sun" ), Color,
                  Tooltip( "Tint of the sky and the solar disk as seen by the camera. Scene surfaces are lit "
                           "by the directional light's own Colour and Intensity, not by this." ) )
        glm::vec3 SunColor = { 1.00f, 0.96f, 0.88f };

        // DEGREES and a DIAMETER, unlike the radians-and-radius number this replaces: degrees is what an
        // artist reads, and the default being 2.29 deg - four times the real sun - is worth being able to
        // see. The shaders still take the angular RADIUS in radians; the conversion happens once, in C++.
        PROPERTY( DisplayName( "Sun Angular Diameter" ), Category( "Sun" ), Range( 0.1f, 10.0f ), Units( "deg" ),
                  Tooltip( "Apparent size of the solar disk. The real sun is 0.53 deg." ) )
        float SunAngularDiameter = 2.2918f;

        PROPERTY( DisplayName( "Sun Glow" ), Category( "Sun" ), Range( 0.0f, 5.0f ) )
        float SunGlow = 1.0f;

        PROPERTY( DisplayName( "Sunset Color" ), Category( "Sun" ), Color )
        glm::vec3 SunsetColor = { 1.00f, 0.42f, 0.18f };

        PROPERTY( DisplayName( "Sunset Intensity" ), Category( "Sun" ), Range( 0.0f, 3.0f ) )
        float SunsetIntensity = 1.0f;

        PROPERTY( DisplayName( "Star Intensity" ), Category( "Night Sky" ), Range( 0.0f, 5.0f ) )
        float StarIntensity = 1.0f;

        PROPERTY( DisplayName( "Drive Sun From Time Of Day" ), Category( "Time Of Day" ) )
        bool DriveSunFromTimeOfDay = false;

        PROPERTY( DisplayName( "Time Of Day" ), Category( "Time Of Day" ), Range( 0.0f, 24.0f ), Units( "h" ),
                  EditCondition( "DriveSunFromTimeOfDay" ) )
        float TimeOfDay = 12.0f;

        PROPERTY( DisplayName( "Day Length" ), Category( "Time Of Day" ), Range( 0.0f, 86400.0f ), Units( "s" ),
                  EditCondition( "DriveSunFromTimeOfDay" ),
                  Tooltip( "Real seconds per in-game day. 0 freezes the sun at Time Of Day." ) )
        float DayLengthSeconds = 600.0f;

        PROPERTY( DisplayName( "Latitude" ), Category( "Time Of Day" ), Range( -90.0f, 90.0f ), Units( "deg" ),
                  EditCondition( "DriveSunFromTimeOfDay" ) )
        float Latitude = 45.0f;

        PROPERTY( DisplayName( "North Offset" ), Category( "Time Of Day" ), Range( 0.0f, 360.0f ), Units( "deg" ),
                  EditCondition( "DriveSunFromTimeOfDay" ) )
        float NorthOffset = 0.0f;

        PROPERTY( DisplayName( "Auto Rebake" ), Category( "Environment Lighting" ), Advanced )
        bool AutoRebakeEnvironment = true;

        PROPERTY( DisplayName( "Rebake Sun Angle" ), Category( "Environment Lighting" ), Range( 0.25f, 45.0f ),
                  Units( "deg" ), Advanced, EditCondition( "AutoRebakeEnvironment" ) )
        float RebakeSunAngleThreshold = 5.0f;

        PROPERTY( DisplayName( "Environment Resolution" ), Category( "Environment Lighting" ), Advanced,
                  Tooltip( "Size of the baked sky cubemap. High costs 32 MiB for the panorama alone, per "
                           "live scene renderer." ) )
        SkyEnvironmentResolution EnvironmentResolution = SkyEnvironmentResolution::Medium;

        PROPERTY( DisplayName( "Preset" ), Category( "Atmosphere" ), ReadOnly,
                  Tooltip( "Which preset the palette came from. Reverts to Custom as soon as any sky colour "
                           "or sun value is edited." ) )
        SkyPreset ActivePreset = SkyPreset::Custom;
    };

    struct SkyAtmosphereComponent
    {
        SkyAtmosphereData Data;

        // Transient (no PROPERTY -> not reflected, not serialized): the editor's "Bake Sky IBL" button and
        // the offscreen renderers raise it for one frame; the sky system consumes and clears it.
        bool RequestBake = false;
    };
} // namespace Desert::ECS
