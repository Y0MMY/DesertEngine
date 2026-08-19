#pragma once

#include "System.hpp"

#include <Engine/ECS/Components.hpp>
#include <Engine/Graphic/Render/Commands/VolumetricCloudCommand.hpp>

#include <Common/Core/Logger.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Desert::ECS
{
    /**
     * @brief Collects the scene's volumetric cloud layer for the frame.
     *
     * A render-data collector with exactly one piece of state of its own: the accumulated wind drift.
     * The GPU-owning half is Graphic::System::VolumetricCloudRenderer.
     *
     * Safe to run in parallel with the other collectors: it reads component state and advances its own
     * scalar, and touches nothing else.
     */
    class VolumetricCloudECSSystem final : public System
    {
    public:
        using System::System;

        bool CanRunParallel() const override
        {
            return true;
        }

        void Update( entt::registry& registry, Graphic::Render::RenderCommandBuffer& renderCommandBuffer,
                     const Common::Timestep& ts ) override
        {
            std::vector<entt::entity> entities;
            auto                      view = registry.view<ECS::VolumetricCloudComponent>();
            for ( const auto entity : view )
                entities.push_back( entity );

            if ( entities.empty() )
            {
                renderCommandBuffer.Emplace<Graphic::Render::VolumetricCloudCommand>(
                     false, ECS::VolumetricCloudData{}, glm::vec3( 0.0f ) );
                return;
            }

            // Lowest UUID drives the frame, exactly as the sky and the fog pick theirs. "Whichever entity
            // entt happened to visit first" changes when an unrelated component is added somewhere else,
            // and a sky that changes for no visible reason is a hard bug to even describe.
            size_t chosen = 0;
            for ( size_t i = 1; i < entities.size(); ++i )
            {
                if ( EntityId( registry, entities[i] ) < EntityId( registry, entities[chosen] ) )
                    chosen = i;
            }

            if ( entities.size() > 1 && !m_DuplicateLogged )
            {
                LOG_WARN( "[Clouds] {} Volumetric Cloud components in the scene; the one on entity '{}' "
                          "drives the frame (lowest id). The others are ignored.",
                          entities.size(), EntityName( registry, entities[chosen] ) );
                m_DuplicateLogged = true;
            }

            const auto& clouds = registry.get<ECS::VolumetricCloudComponent>( entities[chosen] );

            AdvanceWind( clouds.Data, ts.GetSeconds() );

            renderCommandBuffer.Emplace<Graphic::Render::VolumetricCloudCommand>( true, clouds.Data,
                                                                                  m_WindOffset );
        }

    private:
        /**
         * The wind moves the SAMPLE POSITION rather than the data, so the drift is one accumulated vector
         * and costs nothing per sample. Two consequences worth stating, because both are easy to lose:
         *
         *   * The offset accumulates rather than being derived from an absolute clock. A clock would make
         *     the sky's position depend on how long the editor had been open, so two screenshots of the
         *     same scene would never match — and every visual comparison in this programme depends on
         *     them matching.
         *   * A zero-length direction leaves the sky still instead of producing a NaN. `normalize` of a
         *     zero vector is undefined, and the NaN would propagate into every sample position and render
         *     as a black sky with nothing in the log.
         */
        void AdvanceWind( const VolumetricCloudData& data, float seconds )
        {
            if ( !data.Enabled || data.WindSpeed <= 0.0f )
                return;

            const float lengthSquared = glm::dot( data.WindDirection, data.WindDirection );
            if ( lengthSquared <= 1e-12f )
                return;

            const glm::vec3 direction = data.WindDirection / glm::sqrt( lengthSquared );
            m_WindOffset += direction * ( data.WindSpeed * seconds );
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

        glm::vec3 m_WindOffset{ 0.0f };

        // Describes the SCENE, not the frame, so it is said once.
        bool m_DuplicateLogged = false;
    };
} // namespace Desert::ECS
