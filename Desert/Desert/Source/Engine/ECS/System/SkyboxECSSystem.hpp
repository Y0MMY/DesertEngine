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

        // Render-data collector (only touches sky component state) — safe to run concurrently with the other
        // collectors.
        bool CanRunParallel() const override
        {
            return true;
        }

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

            // The procedural atmosphere. Its presence-and-Enabled is what selects the Sky pass mode, the
            // role the Skybox component's `Procedural` bool used to play.
            bool atmosphereEnabled = false;
            {
                auto atmospheres = registry.view<ECS::SkyAtmosphereComponent>();
                for ( const auto atmosphereEntity : atmospheres )
                {
                    auto& atmosphere = registry.get<ECS::SkyAtmosphereComponent>( atmosphereEntity );

                    // One-shot Bake request from the editor: forward it for this frame, then clear it.
                    const bool bakeNow     = atmosphere.RequestBake;
                    atmosphere.RequestBake = false;

                    atmosphereEnabled = atmosphere.Data.Enabled;

                    Graphic::SkySettings sky;
                    sky.ZenithColor     = atmosphere.Data.ZenithColor;
                    sky.HorizonColor    = atmosphere.Data.HorizonColor;
                    sky.GroundColor     = atmosphere.Data.GroundColor;
                    sky.NightColor      = atmosphere.Data.NightColor;
                    sky.SunColor        = atmosphere.Data.SunColor;
                    sky.SunsetColor     = atmosphere.Data.SunsetColor;
                    sky.SkyBrightness   = atmosphere.Data.SkyBrightness;
                    sky.HorizonFalloff  = atmosphere.Data.HorizonFalloff;
                    sky.SunGlow         = atmosphere.Data.SunGlow;
                    sky.SunsetIntensity = atmosphere.Data.SunsetIntensity;
                    sky.StarIntensity   = atmosphere.Data.StarIntensity;

                    // The component authors the sun as an angular DIAMETER in degrees, because that is the
                    // number an artist reads; the shaders want the angular RADIUS in radians. One
                    // conversion, here, so no shader ever has to know which of the two it was handed.
                    const float sunAngularRadius = glm::radians( atmosphere.Data.SunAngularDiameter ) * 0.5f;

                    // Procedural-sky config always flows to the renderer (it toggles the Sky-pass mode).
                    renderCommandBuffer.Emplace<Graphic::Render::ProceduralSkyCommand>(
                         atmosphere.Data.Enabled, sunDir, atmosphere.Data.SunIntensity, sunAngularRadius, bakeNow,
                         Graphic::CloudSettings{}, sky );
                    break;
                }
            }

            // The HDR cubemap is the other Sky-pass mode: only when no atmosphere is driving the sky, and
            // only if an asset is assigned.
            if ( !atmosphereEnabled )
            {
                auto skyboxes = registry.view<ECS::SkyboxComponent>();
                for ( const auto skyboxEntity : skyboxes )
                {
                    const auto& skybox = registry.get<ECS::SkyboxComponent>( skyboxEntity );
                    if ( auto skyboxAsset =
                              Runtime::ResourceRegistry::GetSkyboxService()->Get( skybox.SkyboxHandle ) )
                    {
                        renderCommandBuffer.Emplace<Graphic::Render::SkyboxCommand>( skyboxAsset,
                                                                                      skybox.Intensity );
                    }
                    break;
                }
            }
        }
    };
} // namespace Desert::ECS