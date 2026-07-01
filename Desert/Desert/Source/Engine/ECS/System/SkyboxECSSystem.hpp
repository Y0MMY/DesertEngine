#pragma once

#include "System.hpp"

#include <Engine/ECS/Components.hpp>
#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Assets/AssetManager.hpp>

#include <Engine/Graphic/Render/Commands/SkyboxCommand.hpp>
#include <Engine/Graphic/Render/Commands/ProceduralSkyCommand.hpp>

#include <glm/glm.hpp>

namespace Desert::ECS
{
    class SkyboxECSSystem : public System
    {
    public:
        using System::System;

        void Update( entt::registry& registry, Graphic::Render::RenderCommandBuffer& renderCommandBuffer,
                     const Common::Timestep& ts ) override
        {
            // Sun = the directional light. Its direction lives in TransformComponent.Translation; the
            // toward-sun direction (what the atmosphere wants) is the negated, normalized vector.
            glm::vec3 sunDir = glm::normalize( glm::vec3( 0.3f, 0.9f, 0.3f ) );
            {
                auto dirLights = registry.view<ECS::DirectionLightComponent, ECS::TransformComponent>();
                for ( const auto e : dirLights )
                {
                    const auto& t = dirLights.get<ECS::TransformComponent>( e );
                    if ( glm::length( t.Translation ) > 1e-4f )
                    {
                        sunDir = -glm::normalize( t.Translation );
                        break;
                    }
                }
            }

            const auto& skyboxes = registry.view<ECS::SkyboxComponent>();
            for ( const auto skyboxEntity : skyboxes )
            {
                auto& skybox = registry.get<ECS::SkyboxComponent>( skyboxEntity );

                // One-shot Bake request from the editor: forward it for this frame, then clear it.
                const bool bakeNow = skybox.RequestBake;
                skybox.RequestBake = false;

                Graphic::CloudSettings clouds;
                clouds.Enabled    = skybox.EnableClouds;
                clouds.Coverage   = skybox.CloudCoverage;
                clouds.Density    = skybox.CloudDensity;
                clouds.Tiling     = skybox.CloudTiling;
                clouds.Brightness = skybox.CloudBrightness;
                clouds.WindSpeed  = skybox.CloudWindSpeed;

                Graphic::SkySettings sky;
                sky.ZenithColor     = skybox.ZenithColor;
                sky.HorizonColor    = skybox.HorizonColor;
                sky.GroundColor     = skybox.GroundColor;
                sky.NightColor      = skybox.NightColor;
                sky.SunColor        = skybox.SunColor;
                sky.SunsetColor     = skybox.SunsetColor;
                sky.SkyBrightness   = skybox.SkyBrightness;
                sky.HorizonFalloff  = skybox.HorizonFalloff;
                sky.SunGlow         = skybox.SunGlow;
                sky.SunsetIntensity = skybox.SunsetIntensity;
                sky.StarIntensity   = skybox.StarIntensity;

                // Procedural-sky config always flows to the renderer (it toggles the Sky-pass mode).
                renderCommandBuffer.Emplace<Graphic::Render::ProceduralSkyCommand>(
                     skybox.Procedural, sunDir, skybox.SunIntensity, skybox.SunDiskRadius, bakeNow, clouds, sky );

                // The HDR cubemap is only needed when NOT procedural (and only if an asset is assigned).
                if ( !skybox.Procedural )
                {
                    if ( auto skyboxAsset =
                              Runtime::ResourceRegistry::GetSkyboxService()->Get( skybox.SkyboxHandle ) )
                    {
                        renderCommandBuffer.Emplace<Graphic::Render::SkyboxCommand>( skyboxAsset );
                    }
                }
                break;
            }
        }
    };
} // namespace Desert::ECS