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
        explicit System( const std::weak_ptr<Graphic::SceneRenderer>&    sceneRenderer,
                         const std::weak_ptr<Runtime::ResourceRegistry>& resourceRegistry )
             : m_Renderer( sceneRenderer ), m_ResourceRegistry( resourceRegistry )
        {
        }

        virtual ~System() = default;

        virtual void Update( entt::registry& registry, const Common::Timestep& ts ) = 0;

        virtual void OnInit( entt::registry& registry )
        {
        }

        virtual void OnShutdown( entt::registry& registry )
        {
        }

    protected:
        const std::weak_ptr<Graphic::SceneRenderer>    m_Renderer;
        const std::weak_ptr<Runtime::ResourceRegistry> m_ResourceRegistry;
    };

} // namespace Desert::ECS