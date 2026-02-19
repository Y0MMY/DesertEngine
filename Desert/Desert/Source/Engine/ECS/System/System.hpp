#pragma once

#include <entt/entt.hpp>
#include <Common/Core/Timestep.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::Graphic
{
    class SceneRenderer;
} // namespace Desert::Graphic

namespace Desert::ECS
{
    class System
    {
    public:
        explicit System( const std::weak_ptr<Graphic::SceneRenderer>& sceneRenderer ) : m_Renderer( sceneRenderer )
        {
        }

        System( const System& )            = delete;
        System& operator=( const System& ) = delete;
        System( System&& )                 = delete;
        System& operator=( System&& )      = delete;

        virtual ~System() = default;

        virtual void Update( entt::registry& registry, const Common::Timestep& ts ) = 0;

    protected:
        const std::weak_ptr<Graphic::SceneRenderer> m_Renderer;
    };

} // namespace Desert::ECS