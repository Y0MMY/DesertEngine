#pragma once

#include "System.hpp"
#include "SystemRules.hpp"

#include <Engine/ECS/Components.hpp>
#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Graphic/SkyRules.hpp>
#include <Engine/Graphic/SkySettings.hpp>
#include <Engine/Assets/AssetManager.hpp>

#include <Engine/Graphic/Render/Commands/SkyboxCommand.hpp>
#include <Engine/Graphic/Render/Commands/ProceduralSkyCommand.hpp>

#include <Common/Core/Logger.hpp>

#include <glm/glm.hpp>

#include <string>
#include <vector>

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
            const auto sun = ResolveAtmosphereSun( registry );

            // --- Which sky entity drives the frame ---------------------------------------------------
            // Lowest UUID, not "whichever entity entt visited first": the old rule broke out of the loop
            // after one entity, and that order changes when an unrelated component is added elsewhere.
            std::vector<entt::entity> skyEntities;
            std::vector<uint64_t>     skyIds;
            {
                auto atmospheres = registry.view<ECS::SkyAtmosphereComponent>();
                for ( const auto entity : atmospheres )
                {
                    skyEntities.push_back( entity );
                    skyIds.push_back( EntityId( registry, entity ) );
                }
            }

            const auto primarySky = Graphic::SelectPrimarySky( skyIds );
            if ( skyEntities.size() > 1 && !m_DuplicateSkyLogged )
            {
                LOG_WARN( "[SkyAtmosphere] {} Sky Atmosphere components in the scene; the one on entity '{}' "
                          "drives the frame (lowest id). The others are ignored: {}",
                          skyEntities.size(), EntityName( registry, skyEntities[*primarySky] ),
                          OtherSkyNames( registry, skyEntities, *primarySky ) );
                m_DuplicateSkyLogged = true;
            }

            bool atmosphereEnabled = false;
            if ( primarySky )
            {
                auto& atmosphere = registry.get<ECS::SkyAtmosphereComponent>( skyEntities[*primarySky] );

                // One-shot Bake request from the editor: forward it for this frame, then clear it.
                const bool bakeNow     = atmosphere.RequestBake;
                atmosphere.RequestBake = false;

                atmosphereEnabled = atmosphere.Data.Enabled;

                // ONE conversion from the authored component to the renderer's transport struct — the
                // degrees-to-radians, diameter-to-radius and kilometres-to-world-units steps all live in it.
                renderCommandBuffer.Emplace<Graphic::Render::ProceduralSkyCommand>(
                     atmosphere.Data.Enabled, sun, bakeNow, Graphic::MakeSkySettings( atmosphere.Data ) );
            }

            // The HDR cubemap is the other Sky-pass mode: only when no atmosphere is driving the sky, and
            // only if an asset is assigned.
            if ( Graphic::ResolveSkyMode( atmosphereEnabled, /*hasHdrSkybox=*/true ) ==
                 Graphic::SkyMode::HdrCubemap )
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

    private:
        static uint64_t EntityId( entt::registry& registry, entt::entity entity )
        {
            if ( auto* id = registry.try_get<ECS::UUIDComponent>( entity ) )
                return static_cast<uint64_t>( id->UUID );
            return 0;
        }

        static std::string EntityName( entt::registry& registry, entt::entity entity )
        {
            if ( auto* tag = registry.try_get<ECS::TagComponent>( entity ) )
                return tag->Tag;
            return "<unnamed>";
        }

        static std::string OtherSkyNames( entt::registry& registry, const std::vector<entt::entity>& entities,
                                          size_t chosen )
        {
            std::string names;
            for ( size_t i = 0; i < entities.size(); ++i )
            {
                if ( i == chosen )
                    continue;
                if ( !names.empty() )
                    names += ", ";
                names += '\'' + EntityName( registry, entities[i] ) + '\'';
            }
            return names;
        }

        // Direction TOWARD the sun for this frame. The choice of WHICH light is a pure rule
        // (Rules::SelectAtmosphereSun); this function only fetches its arguments and reports what it said.
        glm::vec3 ResolveAtmosphereSun( entt::registry& registry )
        {
            std::vector<Rules::SunCandidate> candidates;
            std::vector<entt::entity>        entities;

            auto dirLights = registry.view<ECS::DirectionLightComponent, ECS::TransformComponent>();
            for ( const auto entity : dirLights )
            {
                const auto& transform = dirLights.get<ECS::TransformComponent>( entity );
                const auto& light     = dirLights.get<ECS::DirectionLightComponent>( entity );

                entities.push_back( entity );
                candidates.push_back( Rules::SunCandidate{
                     .Id             = EntityId( registry, entity ),
                     .Marked         = light.Data.AtmosphereSunLight,
                     .Index          = light.Data.AtmosphereSunLightIndex,
                     .DirectionValid = Rules::IsSunDirectionValid( transform.Translation ) } );
            }

            // v1 renders exactly one directional light, so index 0 is the only one that can be the sun.
            const auto selection = Rules::SelectAtmosphereSun( candidates, /*wantedIndex=*/0 );

            if ( !m_SunSelectionLogged )
            {
                m_SunSelectionLogged = true;

                for ( const size_t i : selection.Collisions )
                    LOG_WARN( "[SkyAtmosphere] Entity '{}' is also marked as Atmosphere Sun Light at index 0; "
                              "the lowest-id light drives the sky and this one is ignored.",
                              EntityName( registry, entities[i] ) );

                for ( const size_t i : selection.WrongIndex )
                    LOG_WARN( "[SkyAtmosphere] Entity '{}' is marked as Atmosphere Sun Light at index {}, but "
                              "the engine renders one directional light — only index 0 drives the sky.",
                              EntityName( registry, entities[i] ), candidates[i].Index );

                if ( selection.Fallback && selection.Chosen )
                    LOG_INFO( "[SkyAtmosphere] No directional light is marked as the Atmosphere Sun Light; "
                              "'{}' (lowest id) drives the sky.",
                              EntityName( registry, entities[*selection.Chosen] ) );

                if ( !selection.Chosen )
                    LOG_INFO( "[SkyAtmosphere] No directional light with a usable direction; the sky falls "
                              "back to its documented default sun." );
            }

            if ( !selection.Chosen )
                return Rules::FallbackAtmosphereSunDirection();

            const auto& transform = registry.get<ECS::TransformComponent>( entities[*selection.Chosen] );
            return Rules::AtmosphereSunDirection( transform.Translation );
        }

        // Both of these describe the SCENE, not the frame, so they are said once. A per-frame LOG_WARN is
        // how a real message becomes invisible.
        bool m_SunSelectionLogged = false;
        bool m_DuplicateSkyLogged = false;
    };
} // namespace Desert::ECS
