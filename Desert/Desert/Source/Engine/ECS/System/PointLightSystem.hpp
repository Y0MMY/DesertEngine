#pragma once

#include "System.hpp"

#include <Engine/ECS/Components.hpp>

namespace Desert::ECS
{
    class PointLightRenderSystem : public System
    {
    public:
        using System::System;

        void Update( entt::registry& registry, const Common::Timestep& ts ) override
        {
            const auto& renderer = m_Renderer.lock();
            if ( renderer )
            {
                auto meshView = registry.view<PointLightComponent, TransformComponent>();
                meshView.each(
                     [&]( auto entity, const auto& pointlight, const auto& transform )
                     {
                         renderer->AddPointLight( { .Color     = pointlight.Color,
                                                    .Intensity = pointlight.Intensity,
                                                    .Position  = transform.Translation,
                                                    .Radius    = pointlight.Radius } );
                     } );
            }
        }

        void OnInit( entt::registry& registry ) override
        {
            // Any initialization logic if needed
        }

        void OnShutdown( entt::registry& registry ) override
        {
            // Any cleanup logic if needed
        }
    };
} // namespace Desert::ECS