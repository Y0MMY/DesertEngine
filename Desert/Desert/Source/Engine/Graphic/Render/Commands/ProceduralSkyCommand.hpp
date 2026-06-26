#pragma once

#include "../RenderCommand.hpp"
#include <Engine/Graphic/CloudSettings.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic::Render
{
    // Carries the procedural-sky configuration from the ECS (SkyboxComponent + directional light) to the
    // SkyboxRenderer. The sun direction is the toward-sun direction (= -directional light direction).
    struct ProceduralSkyCommand : RenderCommand
    {
        bool          Enabled;
        glm::vec3     SunDir;
        float         SunIntensity;
        float         SunDiskRadius;
        bool          BakeNow; // one-shot: editor Bake button requested an IBL rebuild this frame
        CloudSettings Clouds;

        ProceduralSkyCommand( bool enabled, const glm::vec3& sunDir, float sunIntensity, float sunDiskRadius,
                              bool bakeNow, const CloudSettings& clouds )
             : Enabled( enabled ), SunDir( sunDir ), SunIntensity( sunIntensity ),
               SunDiskRadius( sunDiskRadius ), BakeNow( bakeNow ), Clouds( clouds )
        {
        }

        void Execute( SceneRenderer& renderer ) override
        {
            renderer.SetProceduralSky( Enabled, SunDir, SunIntensity, SunDiskRadius, BakeNow, Clouds );
        }
    };
} // namespace Desert::Graphic::Render
