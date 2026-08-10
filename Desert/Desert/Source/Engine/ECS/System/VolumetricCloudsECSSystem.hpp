#pragma once

#include "System.hpp"

#include <Engine/ECS/Components.hpp>
#include <Engine/Graphic/Render/Commands/VolumetricCloudsCommand.hpp>

#include <Common/Core/Logger.hpp>

#include <cstdint>
#include <string>
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
            std::vector<entt::entity> entities;
            auto                      view = registry.view<ECS::VolumetricCloudsComponent>();
            for ( const auto entity : view )
                entities.push_back( entity );

            if ( entities.empty() )
            {
                renderCommandBuffer.Emplace<Graphic::Render::VolumetricCloudsCommand>(
                     false, ECS::VolumetricCloudData{} );
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
            renderCommandBuffer.Emplace<Graphic::Render::VolumetricCloudsCommand>( true, clouds.Data );
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

        // Describes the SCENE, not the frame, so it is said once.
        bool m_DuplicateLogged = false;
    };
} // namespace Desert::ECS
