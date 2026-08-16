#pragma once

#include "System.hpp"

#include <Engine/ECS/Components.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/Graphic/Clouds/CloudVolumeInstance.hpp>
#include <Engine/Graphic/Clouds/CloudVolumePlacement.hpp>
#include <Engine/Graphic/Render/Commands/VolumetricCloudsCommand.hpp>

#include <Common/Core/Logger.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Desert::ECS
{
    /**
     * @brief Collects the scene's volumetric-cloud settings for the frame.
     *
     * A pure render-data collector — it reads one component and emits one command, exactly as
     * SkyboxECSSystem does for the sky, and takes no decision the renderer could not have taken. The
     * GPU-owning half is Graphic::System::VolumetricCloudRenderer; the NOISE volumes are a different
     * system's business (ECS::CloudNoiseECSSystem), because they are shared process-wide and this one
     * is per-scene.
     *
     * Safe to run in parallel with the other collectors: it only reads cloud component state.
     */
    class VolumetricCloudsECSSystem final : public System
    {
    public:
        using System::System;

        bool CanRunParallel() const override
        {
            return true;
        }

        void Update( entt::registry& registry, Graphic::Render::RenderCommandBuffer& renderCommandBuffer,
                     const Common::Timestep& /*ts*/ ) override
        {
            Graphic::CloudVolumePlacements volumes = CollectVolumes( registry );

            std::vector<entt::entity> entities;
            auto                      view = registry.view<ECS::VolumetricCloudsComponent>();
            for ( const auto entity : view )
                entities.push_back( entity );

            if ( entities.empty() )
            {
                renderCommandBuffer.Emplace<Graphic::Render::VolumetricCloudsCommand>(
                     false, ECS::VolumetricCloudData{}, std::move( volumes ) );
                return;
            }

            // Lowest UUID drives the frame, exactly as the sky picks its atmosphere and the noise system
            // picks its seeds. "Whichever entity entt happened to visit first" changes when an unrelated
            // component is added somewhere else, and a cloudscape that changes for no visible reason is
            // a hard bug to even describe.
            size_t chosen = 0;
            for ( size_t i = 1; i < entities.size(); ++i )
            {
                if ( EntityId( registry, entities[i] ) < EntityId( registry, entities[chosen] ) )
                    chosen = i;
            }

            if ( entities.size() > 1 && !m_DuplicateLogged )
            {
                LOG_WARN( "[Clouds] {} Volumetric Clouds components in the scene; the one on entity '{}' "
                          "drives the frame (lowest id). The others are ignored.",
                          entities.size(), EntityName( registry, entities[chosen] ) );
                m_DuplicateLogged = true;
            }

            const auto& clouds = registry.get<ECS::VolumetricCloudsComponent>( entities[chosen] );
            renderCommandBuffer.Emplace<Graphic::Render::VolumetricCloudsCommand>( true, clouds.Data,
                                                                                   std::move( volumes ) );
        }

    private:
        /**
         * Every enabled hero cloud in the scene, with the world matrix that places it.
         *
         * SORTED SHADOW CASTERS FIRST, and that ordering is a contract with the shaders: the shadow pass
         * marches the first u_VoxelShadowCount records and the view marches all of them, so Casts Cloud
         * Shadow becomes a shorter loop instead of a per-sample flag test. std::stable_partition and not
         * a sort, because within each group the registry's own order must not shuffle from frame to frame
         * — the atlas leases the tiles in this order, and a cloudscape that reshuffles for no visible
         * reason is a hard bug to even describe.
         *
         * The cap is reported ONCE per scene, with the numbers: overflowing it silently is how a scene
         * ends up with a hero cloud that exists in the outliner and nowhere in the sky.
         */
        Graphic::CloudVolumePlacements CollectVolumes( entt::registry& registry )
        {
            Graphic::CloudVolumePlacements volumes;

            auto view = registry.view<ECS::CloudVolumeComponent, ECS::TransformComponent>();
            for ( const auto entity : view )
            {
                const auto& volume = registry.get<ECS::CloudVolumeComponent>( entity );

                // Enabled is the zero-cost gate: a disabled hero cloud is not gathered, so it takes no
                // atlas tile, no instance slot and no sample.
                if ( !volume.Data.Enabled || static_cast<uint64_t>( volume.Data.Volume ) == 0 )
                    continue;

                if ( volumes.size() >= Graphic::kMaxCloudVolumeInstances )
                {
                    if ( !m_CapLogged )
                    {
                        LOG_WARN( "[CloudVolumes] More than {} enabled Cloud Volume entities in the scene; "
                                  "the rest are not rendered. The atlas holds {} tiles — point several "
                                  "entities at the same .dvol to share one.",
                                  Graphic::kMaxCloudVolumeInstances, Graphic::kMaxCloudVolumeInstances );
                        m_CapLogged = true;
                    }
                    break;
                }

                volumes.push_back( Graphic::CloudVolumePlacement{
                     .WorldTransform = ECS::Entity( entity, registry ).GetWorldTransform(),
                     .Data           = volume.Data } );
            }

            std::stable_partition( volumes.begin(), volumes.end(),
                                   []( const Graphic::CloudVolumePlacement& placement )
                                   { return placement.Data.CastsCloudShadow; } );

            return volumes;
        }

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

        // Describes the SCENE, not the frame, so it is said once.
        bool m_DuplicateLogged = false;
        bool m_CapLogged       = false;
    };
} // namespace Desert::ECS
