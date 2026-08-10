#pragma once

#include <Engine/Graphic/SkySettings.hpp>

#include <glm/glm.hpp>

namespace Desert::ShaderResources
{
    class StorageBuffer;
}

namespace Desert::Graphic
{
    // Per-frame, EVALUATED state of the sky — the runtime form other renderers consume via
    // SceneRenderer::GetAtmosphere(), mirroring WindEnv / GetWind(). The volumetric cloud pass is its
    // reason for existing: cloud lighting and sky lighting must come from one sun and one sky, or they
    // disagree in a way nobody finds by looking.
    //
    // THIS STRUCT IS CLOSED, and deliberately narrow. It carries EVALUATED QUANTITIES, never the authoring
    // representation they came from: no SkyAtmosphereData, no SkySettings, no palette. A consumer that
    // received the palette would be coupled to the sky's most volatile surface — every colour added,
    // reordered or reinterpreted would break its binary layout. What is shared instead is the COMPUTATION:
    // in C++ through the numbers below, and in GLSL through Common/Atmosphere.glslh's EvaluateSky.
    //
    // ParamsBuffer is consistent with that rule rather than an exception to it: a handle is not a layout.
    // A consumer binds it (`SetStorageBuffer( itsOwnBinding, env.ParamsBuffer )`) and unpacks it in GLSL
    // through the shared loaders; it can never read a field of it from C++.
    struct AtmosphereEnv
    {
        glm::vec3 SunDirection{ 0.0f, 1.0f, 0.0f }; // normalized, TOWARD the sun — the engine's one negation
        glm::vec3 SunIrradiance{ 0.0f };            // linear RGB, SunColor * SunIntensity
        glm::vec3 ZenithRadiance{ 0.0f };           // linear RGB ambient from above (day/night blended)
        glm::vec3 GroundRadiance{ 0.0f };           // linear RGB ambient from below

        float SunAngularRadius = 0.0f; // radians
        float NightFactor      = 0.0f; // 1 at night, 0 in daylight — matches Atmosphere.glslh's day blend
        float PlanetRadius     = 0.0f; // WORLD UNITS (centimetres)

        // false when there is no enabled sky component, or no atmosphere sun to drive it. A consumer that
        // draws anyway is drawing against last frame's sun.
        bool Valid = false;

        // OPAQUE handle to the packed sky-parameter SSBO. Non-owning: the SkyboxRenderer of this
        // SceneRenderer owns the buffer, and it is null exactly when Valid is false.
        ShaderResources::StorageBuffer* ParamsBuffer = nullptr;
    };

    // The C++ half of "share the computation": the quantities below are read off the same gradient the
    // shader evaluates, at the two directions that matter for ambient lighting.
    //
    // Straight up, Atmosphere.glslh reduces to `mix(night, zenith, day) * skyBrightness` — the horizon
    // gradient has fully resolved to the zenith colour, and the sunset band (a Gaussian in elevation,
    // exp(-8) up there) and the star field (a sparse hash) are not ambient light. Straight down it reduces
    // to `mix(ground * 0.30, ground, day)`, which the shader mixes in AFTER the brightness multiply — so
    // the ground term is deliberately not scaled by it here either. Getting that ordering wrong is a
    // ground ambient that brightens when the artist raises Sky Brightness and a sky that does not.
    inline AtmosphereEnv EvaluateAtmosphere( const SkySettings& sky, const glm::vec3& towardSun,
                                             ShaderResources::StorageBuffer* paramsBuffer )
    {
        const glm::vec3 dir = glm::normalize( towardSun );

        AtmosphereEnv env;
        env.SunDirection = dir;
        env.SunIrradiance = sky.SunColor * sky.SunIntensity;

        // Identical blend to Atmosphere.glslh: day = smoothstep(-0.10, 0.20, sunDir.y).
        const float day = glm::smoothstep( -0.10f, 0.20f, dir.y );

        env.ZenithRadiance = glm::mix( sky.NightColor, sky.ZenithColor, day ) * sky.SkyBrightness;
        env.GroundRadiance = glm::mix( sky.GroundColor * 0.30f, sky.GroundColor, day );

        env.SunAngularRadius = sky.SunAngularRadius;
        env.NightFactor      = 1.0f - day;
        env.PlanetRadius     = sky.PlanetRadius;
        env.Valid            = true;
        env.ParamsBuffer     = paramsBuffer;
        return env;
    }
} // namespace Desert::Graphic
