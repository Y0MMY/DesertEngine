#pragma once

#include <entt/entt.hpp>
#include <Common/Core/Timestep.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Graphic/Render/RenderCommandBuffer.hpp>

namespace Desert::ECS
{
    class System
    {
    public:
        explicit System() = default;

        System( const System& )            = delete;
        System& operator=( const System& ) = delete;
        System( System&& )                 = delete;
        System& operator=( System&& )      = delete;

        virtual ~System() = default;

        virtual void Update( entt::registry& registry, Graphic::Render::RenderCommandBuffer& renderCommandBuffer,
                             const Common::Timestep& ts ) = 0;
    };

} // namespace Desert::ECS