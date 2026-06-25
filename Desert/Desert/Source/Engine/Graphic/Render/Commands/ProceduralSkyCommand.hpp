#pragma once

#include "../RenderCommand.hpp"

#include <glm/glm.hpp>

namespace Desert::Graphic::Render
{
    // Carries the procedural-sky configuration from the ECS (SkyboxComponent + directional light) to the
    // SkyboxRenderer. The sun direction is the toward-sun direction (= -directional light direction).
    struct ProceduralSkyCommand : RenderCommand
    {
        bool      Enabled;
        glm::vec3 SunDir;
        float     SunIntensity;
        float     SunDiskRadius;

        ProceduralSkyCommand( bool enabled, const glm::vec3& sunDir, float sunIntensity, float sunDiskRadius )
             : Enabled( enabled ), SunDir( sunDir ), SunIntensity( sunIntensity ), SunDiskRadius( sunDiskRadius )
        {
        }

        void Execute( SceneRenderer& renderer ) override
        {
            renderer.SetProceduralSky( Enabled, SunDir, SunIntensity, SunDiskRadius );
        }
    };
} // namespace Desert::Graphic::Render
