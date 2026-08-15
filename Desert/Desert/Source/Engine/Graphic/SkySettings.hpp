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

        // Which model the sky pass evaluates; gates the physical-atmosphere LUT dispatches, so a scene
        // on the artistic gradient pays nothing for the machinery below.
        ECS::SkyModel Model = ECS::SkyModel::ArtisticGradient;

        // ---- Physical atmosphere medium (SkyModel::PhysicalAtmosphere) -----------------------------
        // Coefficients are PER KILOMETRE, the component's own authoring unit — UE's scale x colour pairs
        // collapsed into one coefficient each, which is the second (and last) thing MakeSkySettings
        // converts. Altitudes and scale heights are kilometres. The shaders take these verbatim; the one
        // world-unit quantity in the sky maths (PlanetRadius above) is converted to km inside the shader.
        glm::vec3 RayleighScattering        = { 0.005802f, 0.013558f, 0.0331f }; // /km
        float     RayleighExpDistributionKm = 8.0f;
        glm::vec3 MieScattering             = { 0.003996f, 0.003996f, 0.003996f }; // /km
        glm::vec3 MieAbsorption             = { 0.000444f, 0.000444f, 0.000444f }; // /km
        float     MieAnisotropy             = 0.8f;
        float     MieExpDistributionKm      = 1.2f;
        glm::vec3 OzoneAbsorption           = { 0.000650f, 0.001881f, 0.000085f }; // /km
        float     OzoneTipAltitudeKm        = 25.0f;
        float     OzoneTipValue             = 1.0f;
        float     OzoneTentWidthKm          = 15.0f;
        glm::vec3 GroundAlbedo              = { 0.401978f, 0.401978f, 0.401978f };
        float     AtmosphereHeightKm        = 60.0f;
        float     MultiScatteringFactor     = 1.0f;

        // Art direction of the physical model (UE semantics). The first tints only the sky pixels the
        // screen pass draws; the second is applied inside every scattering integration (Sky-View LUT,
        // IBL bake, and Phase 3's aerial-perspective volume). White is physical.
        glm::vec3 SkyLuminanceFactor                     = { 1.0f, 1.0f, 1.0f };
        glm::vec3 SkyAndAerialPerspectiveLuminanceFactor = { 1.0f, 1.0f, 1.0f };

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

        sky.Model = data.Model;

        // UE's scale x colour split collapses to one per-kilometre coefficient per medium HERE, and
        // nowhere else — the shaders never see the split, so they cannot multiply it twice or not at all.
        sky.RayleighScattering        = data.RayleighScatteringScale * data.RayleighScattering;
        sky.RayleighExpDistributionKm = data.RayleighExponentialDistribution;
        sky.MieScattering             = data.MieScatteringScale * data.MieScattering;
        sky.MieAbsorption             = data.MieAbsorptionScale * data.MieAbsorption;
        sky.MieAnisotropy             = data.MieAnisotropy;
        sky.MieExpDistributionKm      = data.MieExponentialDistribution;
        sky.OzoneAbsorption           = data.OtherAbsorptionScale * data.OtherAbsorption;
        sky.OzoneTipAltitudeKm        = data.AbsorptionTipAltitude;
        sky.OzoneTipValue             = data.AbsorptionTipValue;
        sky.OzoneTentWidthKm          = data.AbsorptionTentWidth;
        sky.GroundAlbedo              = data.GroundAlbedo;
        sky.AtmosphereHeightKm        = data.AtmosphereHeight;
        sky.MultiScatteringFactor     = data.MultiScatteringFactor;

        sky.SkyLuminanceFactor                     = data.SkyLuminanceFactor;
        sky.SkyAndAerialPerspectiveLuminanceFactor = data.SkyAndAerialPerspectiveLuminanceFactor;
        return sky;
    }
} // namespace Desert::Graphic
