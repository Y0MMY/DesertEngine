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
     * @brief Collects the scene's volumetric-cloud LAYERS for the frame.
     *
     * A pure render-data collector — it reads the cloud components and emits one command, exactly as
     * SkyboxECSSystem does for the sky, and takes no decision the renderer could not have taken. The
     * GPU-owning half is Graphic::System::VolumetricCloudRenderer; the NOISE volumes are a different
     * system's business (ECS::CloudNoiseECSSystem), because they are shared process-wide and this one
     * is per-scene.
     *
     * A SECOND CLOUD LAYER IS A SECOND ENTITY. That is the whole authoring model: the component already
     * carries every parameter a layer needs, the Cirrus preset already authors a thin high sheet, and a
     * scene adds a deck plus a sheet by adding a second entity with a Volumetric Clouds component on it.
     * Nothing was added to the component and nothing changed in the file format, which is what makes
     * every scene written before this load and render exactly as it did.
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
                renderCommandBuffer.Emplace<Graphic::Render::VolumetricCloudsCommand>( Graphic::CloudLayerSet{},
                                                                                       std::move( volumes ) );
                return;
            }

            // A DETERMINISTIC ORDER FIRST, then altitude. Sorting by (bottom altitude, UUID) rather than
            // by altitude alone matters because two layers CAN sit at the same height while somebody is
            // dragging a slider, and "whichever entity entt happened to visit first" changes when an
            // unrelated component is added somewhere else — a cloudscape that changes for no visible
            // reason is a hard bug to even describe.
            std::sort( entities.begin(), entities.end(),
                       [&registry]( entt::entity a, entt::entity b )
                       {
                           return LayerOrderBefore(
                                registry.get<ECS::VolumetricCloudsComponent>( a ).Data.LayerBottomAltitude,
                                EntityId( registry, a ),
                                registry.get<ECS::VolumetricCloudsComponent>( b ).Data.LayerBottomAltitude,
                                EntityId( registry, b ) );
                       } );

            // A DISABLED layer is not a layer. Skipping it here rather than passing it through with a
            // flag is what keeps `Enabled` the zero-cost gate it is on every other component: the shell is
            // never intersected, its weather slice is never baked and its shadow slice is never marched.
            Graphic::CloudLayerSet layers;
            uint32_t               enabled = 0;
            std::string            marched;
            for ( const auto entity : entities )
            {
                const auto& clouds = registry.get<ECS::VolumetricCloudsComponent>( entity );
                if ( !clouds.Data.Enabled )
                    continue;

                ++enabled;
                if ( layers.Count < Graphic::kCloudMaxLayers )
                {
                    layers.Layers[layers.Count++] = clouds.Data;
                    if ( !marched.empty() )
                        marched += "', '";
                    marched += EntityName( registry, entity );
                }
            }

            if ( enabled > Graphic::kCloudMaxLayers && !m_DuplicateLogged )
            {
                LOG_WARN( "[Clouds] {} enabled Volumetric Clouds components in the scene; only the lowest "
                          "{} by altitude are marched — the ray plan is built for that many disjoint "
                          "shells. In the sky: '{}'.",
                          enabled, Graphic::kCloudMaxLayers, marched );
                m_DuplicateLogged = true;
            }

            // Count 0 with components present means every one of them is switched off. Said the same way
            // as "no component at all", because it is the same instruction to the renderer: stop marching
            // and give the images back.
            renderCommandBuffer.Emplace<Graphic::Render::VolumetricCloudsCommand>( layers, std::move( volumes ) );
        }

        /**
         * @brief Whether @p entity is the layer whose VIEW-WIDE settings the renderer obeys.
         *
         * Resolution Scale, Temporal Mode, Temporal Blend Factor, Temporal Clamp Scale and Jitter
         * Strength describe the one ray and the one history this view has, and Shape Seed / Detail Seed
         * choose the one noise set the scene leases — so all seven come from a single layer whatever else
         * is in the sky. The Details panel has to name the SAME layer this collector picks, and a second
         * copy of the rule over there would be free to disagree with this one, so the rule lives here and
         * both sides call it.
         *
         * A disabled layer is never the primary: it is not a layer at all, and the lowest ENABLED one is.
         */
        static bool IsPrimaryCloudLayer( entt::registry& registry, entt::entity entity )
        {
            const auto* self = registry.try_get<ECS::VolumetricCloudsComponent>( entity );
            if ( !self || !self->Data.Enabled )
                return false;

            auto view = registry.view<ECS::VolumetricCloudsComponent>();
            for ( const auto other : view )
            {
                if ( other == entity )
                    continue;

                const auto& clouds = registry.get<ECS::VolumetricCloudsComponent>( other );
                if ( !clouds.Data.Enabled )
                    continue;

                if ( LayerOrderBefore( clouds.Data.LayerBottomAltitude, EntityId( registry, other ),
                                       self->Data.LayerBottomAltitude, EntityId( registry, entity ) ) )
                    return false;
            }
            return true;
        }

    private:
        // Lowest bottom altitude first; the UUID breaks a tie. The tie-break is not decoration: two
        // layers CAN sit at the same height while somebody drags a slider, and "whichever entity entt
        // visited first" changes when an unrelated component is added elsewhere in the scene.
        static bool LayerOrderBefore( float altitudeA, uint64_t idA, float altitudeB, uint64_t idB )
        {
            if ( altitudeA != altitudeB )
                return altitudeA < altitudeB;
            return idA < idB;
        }

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
