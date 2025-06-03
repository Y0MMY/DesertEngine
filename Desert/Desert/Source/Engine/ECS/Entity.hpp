#pragma once

#include <entt/entt.hpp>

#include <Engine/ECS/Components.hpp>

namespace Desert::Core
{
    class Scene;
}

namespace Desert::ECS
{
    class Entity final
    {
    public:
        explicit Entity( std::string&& tag, entt::entity handle,
                         const std::reference_wrapper<Core::Scene>& scene );
        ~Entity() = default;

        template <typename EntityT>
        bool HasComponent() const
        {
            return m_Scene.get().GetRegistry().has<EntityT>( m_Handle );
        }

        template <typename EntityT, typename... Args>
        EntityT& AddComponent( Args&&... args ) const
        {
            return m_Scene.get().GetRegistry().emplace<EntityT>( m_Handle, std::forward<Args>( args )... );
        }

        template <typename EntityT>
        EntityT& GetComponent() const
        {
            return m_Scene.get().GetRegistry().get<EntityT>( m_Handle );
        }

    private:
        entt::entity                              m_Handle;
        const std::reference_wrapper<Core::Scene> m_Scene;
    };
} // namespace Desert::ECS