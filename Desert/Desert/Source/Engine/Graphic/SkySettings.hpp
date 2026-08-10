#pragma once

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    // Artistic procedural-sky palette + scalars, carried from ECS::SkyAtmosphereComponent through the render
    // command to the procedural-sky pass (SkyUB). Mirrors the reflected palette fields on that component's
    // data block. Colours are LINEAR.
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
    };
} // namespace Desert::Graphic
