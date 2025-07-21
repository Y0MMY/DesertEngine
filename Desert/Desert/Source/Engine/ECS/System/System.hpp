#pragma once

#include <entt/entt.hpp>
#include <Common/Core/Timestep.hpp>

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
                         const std::weak_ptr<Runtime::ResourceResolver>& resourceManager )
             : m_Renderer( sceneRenderer ), m_ResourceManager( resourceManager )
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
        const std::weak_ptr<Runtime::ResourceResolver> m_ResourceManager;
    };

} // namespace Desert::ECS