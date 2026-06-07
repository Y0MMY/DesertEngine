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
                     glm::mat4 worldTransform = transform.GetTransform();

                     entt::entity current = entity;
                     while ( registry.has<RelationshipComponent>( current ) )
                     {
                         const auto& rel = registry.get<RelationshipComponent>( current );
                         if ( rel.Parent == entt::null ) break;

                         current = rel.Parent;
                         if ( registry.has<TransformComponent>( current ) )
                         {
                             const auto& parentTransform = registry.get<TransformComponent>( current );
                             worldTransform = parentTransform.GetTransform() * worldTransform;
                         }
                     }

                     Graphic::ShaderProtocols::PointLightPayload light{};
                     light.Color     = pointlight.Color;
                     light.Intensity = pointlight.Intensity;
                     light.Position  = glm::vec3(worldTransform[3]); // Extract translation from world matrix
                     light.Radius    = pointlight.Radius;

                     renderCommandBuffer.Emplace<Graphic::Render::PointLightCommand>( light );
                 } );
        }
    };
} // namespace Desert::ECS