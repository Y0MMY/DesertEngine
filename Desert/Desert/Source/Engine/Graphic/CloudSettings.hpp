#pragma once

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    // Procedural flat-layer cloud config (e2gamedev-style; painted in the sky shader, NOT volumetric).
    // Carried from the SkyboxComponent through the render command to the procedural-sky pass.
    struct CloudSettings
    {
        bool  Enabled    = false;
        float Coverage   = 0.5f;  // 0 = clear, 1 = overcast
        float Density    = 1.0f;  // opacity multiplier
        float Tiling     = 1.5f;  // cloud scale
        float Brightness = 1.0f;  // cloud albedo multiplier
        float WindSpeed  = 8.0f;  // per-sky drift RATE (clouds move faster than ground foliage)
        // SHARED scene wind direction (SceneSettings, normalized ground XZ), injected by SceneRenderer so
        // clouds drift the same heading as grass. Not authored on the Skybox — the Skybox only owns the rate.
        glm::vec2 WindDir{ 1.0f, 0.0f };
    };
} // namespace Desert::Graphic
