#pragma once

#include "System.hpp"

#include <Engine/ECS/Components.hpp>

#include <Engine/Graphic/Render/Commands/PointLightCommand.hpp>

namespace Desert::ECS
{
    class PointLightECSSystem : public System
    {
    public:
        using System::System;

        void Update( entt::registry& registry, Graphic::Render::RenderCommandBuffer& renderCommandBuffer,
                     const Common::Timestep& ts ) override
        {

            auto pointLightView = registry.view<PointLightComponent, TransformComponent>();

            pointLightView.each(
                 [&]( auto entity, const auto& pointlight, const auto& transform )
                 {
                     Graphic::ShaderProtocols::PointLightPayload light{};
                     light.Color     = pointlight.Color;
                     light.Intensity = pointlight.Intensity;
                     light.Position  = transform.Translation;
                     light.Radius    = pointlight.Radius;

                     renderCommandBuffer.Emplace<Graphic::Render::PointLightCommand>( light );
                 } );
        }
    };
} // namespace Desert::ECS