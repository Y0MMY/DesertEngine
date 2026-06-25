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
        bool      BakeNow; // one-shot: editor Bake button requested an IBL rebuild this frame

        ProceduralSkyCommand( bool enabled, const glm::vec3& sunDir, float sunIntensity, float sunDiskRadius,
                              bool bakeNow )
             : Enabled( enabled ), SunDir( sunDir ), SunIntensity( sunIntensity ),
               SunDiskRadius( sunDiskRadius ), BakeNow( bakeNow )
        {
        }

        void Execute( SceneRenderer& renderer ) override
        {
            renderer.SetProceduralSky( Enabled, SunDir, SunIntensity, SunDiskRadius, BakeNow );
        }
    };
} // namespace Desert::Graphic::Render
