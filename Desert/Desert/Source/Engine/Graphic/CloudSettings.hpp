#pragma once

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
        float WindSpeed  = 8.0f;  // drift speed
    };
} // namespace Desert::Graphic
