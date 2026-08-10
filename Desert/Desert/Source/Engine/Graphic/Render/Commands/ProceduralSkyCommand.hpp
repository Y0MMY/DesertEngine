#pragma once

#include "../RenderCommand.hpp"
#include <Engine/Graphic/SkySettings.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic::Render
{
    // Carries the procedural-sky configuration from the ECS (SkyAtmosphereComponent + the atmosphere sun)
    // to the SkyboxRenderer. SunDir is the direction TOWARD the sun, already normalized — the engine's one
    // negation happened in ECS::Rules::AtmosphereSunDirection and must not happen again downstream.
    struct ProceduralSkyCommand : RenderCommand
    {
        bool        Enabled;
        glm::vec3   SunDir;
        bool        BakeNow; // one-shot: the editor's Bake button requested an IBL rebuild this frame
        SkySettings Sky;

        ProceduralSkyCommand( bool enabled, const glm::vec3& sunDir, bool bakeNow, const SkySettings& sky )
             : Enabled( enabled ), SunDir( sunDir ), BakeNow( bakeNow ), Sky( sky )
        {
        }

        void Execute( SceneRenderer& renderer ) override
        {
            renderer.SetProceduralSky( Enabled, SunDir, BakeNow, Sky );
        }
    };
} // namespace Desert::Graphic::Render
