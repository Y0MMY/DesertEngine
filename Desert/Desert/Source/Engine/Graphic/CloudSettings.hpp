#pragma once

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    // Procedural flat-layer cloud config (e2gamedev-style; painted in the sky shader, NOT volumetric).
    // Carried through the render command to the procedural-sky pass.
    //
    // NOTHING AUTHORS THIS ANY MORE. The six flat-layer fields it mirrored left SkyboxComponent for the
    // volumetric VolumetricCloudsComponent, so every caller now passes a default-constructed value and the
    // shader's cloud branch is never taken. This struct, the RenderClouds call in ProceduralSky.shader and
    // Common/Clouds.glslh all die together with the flat path when the volumetric pass replaces them —
    // deleting them here would mean reshaping the SkyUB block and its hand-maintained shader mirror for a
    // layout the volumetric work replaces again straight away.
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
