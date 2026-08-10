#pragma once

#include <Engine/ECS/SkyAtmosphereComponent.hpp>
#include <Engine/Graphic/SkyRules.hpp>

#include <glm/glm.hpp>
#include <glm/trigonometric.hpp>

namespace Desert::Graphic
{
    // Everything the sky pass and its IBL bake need from SkyAtmosphereData, in the form they need it in.
    // Carried from the ECS through ProceduralSkyCommand to SkyboxRenderer. Colours are LINEAR.
    //
    // The component's own units are the ARTIST's (an angular diameter in degrees, a planet radius in
    // kilometres); this struct's are the RENDERER's (a radius in radians, a length in world units). The
    // conversion happens exactly once, in MakeSkySettings, so no shader and no renderer ever has to ask
    // which of the two it was handed.
    struct SkySettings
    {
        glm::vec3 ZenithColor  = { 0.08f, 0.26f, 0.70f };
        glm::vec3 HorizonColor = { 0.50f, 0.66f, 0.92f };
        glm::vec3 SunColor     = { 1.00f, 0.96f, 0.88f };
        glm::vec3 SunsetColor  = { 1.00f, 0.42f, 0.18f };
        glm::vec3 GroundColor  = { 0.16f, 0.19f, 0.24f };
        glm::vec3 NightColor   = { 0.010f, 0.020f, 0.050f };

        float SkyBrightness   = 1.0f;
        float HorizonFalloff  = 0.85f;
        float SunGlow         = 1.0f;
        float SunsetIntensity = 1.0f;
        float StarIntensity   = 1.0f;

        // RADIANCE of the sky and of the solar disk — how bright the sun looks IN THE PICTURE. NOT the
        // illuminance the surfaces receive: that is DirectionalLightData::Color x Intensity, which travels
        // a different path (DirectionLightsUB) and is never derived from this one.
        float SunIntensity = 22.0f;

        // Angular RADIUS in RADIANS (the component authors a DIAMETER in degrees).
        float SunAngularRadius = 0.02f;

        // WORLD UNITS (centimetres). The single planet radius in the engine: the volumetric cloud shell
        // reads it from AtmosphereEnv so the sky's horizon and the cloud layer's horizon cannot disagree.
        float PlanetRadius = 636000000.0f;

        // Environment-bake knobs. Quality/performance, never preset-driven.
        bool                          AutoRebakeEnvironment   = true;
        float                         RebakeSunAngleThreshold = 5.0f; // degrees
        ECS::SkyEnvironmentResolution EnvironmentResolution   = ECS::SkyEnvironmentResolution::Medium;
    };

    // The ONLY conversion from the authored component to the renderer's transport struct.
    //
    // There used to be three of these, hand-written: the ECS collector, the mesh-preview viewport and the
    // asset thumbnail renderer each copied the palette field by field — and the thumbnail renderer wrote
    // the same eight literals TWICE, once onto the component and once into the transport. That is how a
    // field added to the component reaches the viewport and not the thumbnails.
    inline SkySettings MakeSkySettings( const ECS::SkyAtmosphereData& data )
    {
        SkySettings sky;
        sky.ZenithColor  = data.ZenithColor;
        sky.HorizonColor = data.HorizonColor;
        sky.SunColor     = data.SunColor;
        sky.SunsetColor  = data.SunsetColor;
        sky.GroundColor  = data.GroundColor;
        sky.NightColor   = data.NightColor;

        sky.SkyBrightness   = data.SkyBrightness;
        sky.HorizonFalloff  = data.HorizonFalloff;
        sky.SunGlow         = data.SunGlow;
        sky.SunsetIntensity = data.SunsetIntensity;
        sky.StarIntensity   = data.StarIntensity;

        sky.SunIntensity = data.SunIntensity;
        // Angular DIAMETER in degrees on the component (what an artist reads), angular RADIUS in radians
        // in the shader (what the Gaussian disk wants).
        sky.SunAngularRadius = glm::radians( data.SunAngularDiameter ) * 0.5f;
        sky.PlanetRadius     = PlanetRadiusToWorldUnits( data.PlanetRadius );

        sky.AutoRebakeEnvironment   = data.AutoRebakeEnvironment;
        sky.RebakeSunAngleThreshold = data.RebakeSunAngleThreshold;
        sky.EnvironmentResolution   = data.EnvironmentResolution;
        return sky;
    }
} // namespace Desert::Graphic
