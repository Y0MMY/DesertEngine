#pragma once

namespace Desert::Graphic
{
    // Engine-generated volumetric-cloud configuration, carried from the SkyboxComponent through the render
    // command to the procedural-sky pass. Mirrors the cloud fields on ECS::SkyboxComponent.
    struct CloudSettings
    {
        bool  Enabled   = false;
        float Coverage  = 0.5f;
        float Density   = 0.6f;
        float Height    = 600.0f; // world-space base altitude of the cloud layer
        float Thickness = 500.0f;
        float WindSpeed = 8.0f;
    };
} // namespace Desert::Graphic
