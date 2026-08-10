#pragma once

#include "System.hpp"

#include <Engine/ECS/Components.hpp>
#include <Engine/Graphic/Clouds/CloudNoiseRules.hpp>
#include <Engine/Graphic/Clouds/CloudNoiseVolumes.hpp>

#include <Common/Core/Logger.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Desert::ECS
{
    /**
     * @brief Keeps the shared cloud noise volumes in step with the scene's VolumetricCloudsComponent.
     *
     * This is the CALLER the generator needs. Without it, "a function that fills a volume" is a function
     * nothing invokes; with it, the volumes appear when a scene grows a cloud component, follow the
     * seeds, answer the editor's regenerate button, and go away when the component does.
     *
     * The system takes no decisions of its own: what to do is Graphic::DecideCloudNoiseAction, a pure
     * function that is unit-tested; this class only reads the registry, calls it, and reports what it
     * said. Everything GPU-shaped lives behind Graphic::CloudNoiseVolumes.
     *
     * NOT parallel-capable (the base class default): every action it can take creates or destroys GPU
     * resources, and the scene runs parallel-capable systems concurrently on the job system.
     */
    class CloudNoiseECSSystem final : public System
    {
    public:
        using System::System;

        ~CloudNoiseECSSystem() override
        {
            // A scene being torn down still holds its lease. Releasing here is what makes closing a scene
            // view free the 8 MiB, instead of leaving it live until the process exits.
            if ( m_Lease.Held )
                Graphic::CloudNoiseVolumes::Get().Release( m_Lease.Key );
        }

        void Update( entt::registry& registry, Graphic::Render::RenderCommandBuffer& /*renderCommandBuffer*/,
                     const Common::Timestep& /*ts*/ ) override
        {
            const Graphic::CloudNoiseDemand demand = CollectDemand( registry );

            auto&      volumes = Graphic::CloudNoiseVolumes::Get();
            const auto action  = Graphic::DecideCloudNoiseAction( m_Lease, demand );

            switch ( action )
            {
                case Graphic::CloudNoiseAction::None:
                    break;

                case Graphic::CloudNoiseAction::Generate:
                    volumes.Acquire( demand.Key, demand.ForceRegenerate );
                    m_Lease = Graphic::CloudNoiseLease{ .Held = true, .Key = demand.Key };
                    break;

                case Graphic::CloudNoiseAction::Regenerate:
                    // Acquire the new key BEFORE releasing the old one when they are the same key, or a
                    // forced rebuild would drop the lease count to zero and destroy the set the rebuild
                    // is about to write into. Ordering this way also keeps a shared set alive across a
                    // seed change made in one of two scenes that were sharing it.
                    volumes.Acquire( demand.Key, demand.ForceRegenerate );
                    volumes.Release( m_Lease.Key );
                    m_Lease = Graphic::CloudNoiseLease{ .Held = true, .Key = demand.Key };
                    break;

                case Graphic::CloudNoiseAction::Release:
                    volumes.Release( m_Lease.Key );
                    m_Lease = Graphic::CloudNoiseLease{};
                    break;
            }
        }

    private:
        // Reads the scene's cloud component and turns it into the demand the rules function decides on.
        // Also CONSUMES the transient regenerate flag: it is a one-frame request, and leaving it raised
        // would rebuild the volumes on every frame from then on.
        Graphic::CloudNoiseDemand CollectDemand( entt::registry& registry )
        {
            std::vector<entt::entity> entities;
            auto                      view = registry.view<ECS::VolumetricCloudsComponent>();
            for ( const auto entity : view )
                entities.push_back( entity );

            if ( entities.empty() )
                return Graphic::CloudNoiseDemand{};

            // Lowest UUID drives the frame, exactly as the sky picks its atmosphere. "Whichever entity
            // entt happened to visit first" changes when an unrelated component is added elsewhere, and
            // a cloudscape that reshuffles for no visible reason is a hard bug to even describe.
            size_t chosen = 0;
            for ( size_t i = 1; i < entities.size(); ++i )
            {
                if ( EntityId( registry, entities[i] ) < EntityId( registry, entities[chosen] ) )
                    chosen = i;
            }

            if ( entities.size() > 1 && !m_DuplicateLogged )
            {
                LOG_WARN( "[CloudNoise] {} Volumetric Clouds components in the scene; the one on entity "
                          "'{}' drives the noise volumes (lowest id). The others are ignored.",
                          entities.size(), EntityName( registry, entities[chosen] ) );
                m_DuplicateLogged = true;
            }

            auto& clouds = registry.get<ECS::VolumetricCloudsComponent>( entities[chosen] );

            const bool force              = clouds.RequestRegenerateNoise;
            clouds.RequestRegenerateNoise = false;

            // A disabled cloud layer is not rendered, so its 8 MiB of noise is 8 MiB of nothing. Treating
            // Enabled as part of the demand means unticking the box actually frees the memory.
            return Graphic::CloudNoiseDemand{
                 .Wanted          = clouds.Data.Enabled,
                 .Key             = Graphic::MakeCloudNoiseKey( clouds.Data.ShapeSeed, clouds.Data.DetailSeed ),
                 .ForceRegenerate = force };
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

        Graphic::CloudNoiseLease m_Lease{};

        // Describes the SCENE, not the frame, so it is said once — a per-frame warning is how a real
        // message becomes invisible.
        bool m_DuplicateLogged = false;
    };
} // namespace Desert::ECS
