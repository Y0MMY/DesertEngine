#pragma once

#include <entt/entt.hpp>
#include <Engine/ECS/Components.hpp>

namespace Desert::ECS
{
    class Entity final
    {
    public:
        explicit Entity( entt::entity handle, entt::registry& registry );
        ~Entity() = default;

        template <typename EntityT>
        bool HasComponent() const
        {
            return m_Registry->has<EntityT>( m_Handle );
        }

        template <typename EntityT, typename... Args>
        decltype( auto ) AddComponent( Args&&... args ) const
        {
            if constexpr ( sizeof...( Args ) == 0 )
            {
                return m_Registry->emplace_or_replace<EntityT>( m_Handle );
            }
            else
            {
                return m_Registry->emplace_or_replace<EntityT>( m_Handle, std::forward<Args>( args )... );
            }
        }

        template <typename EntityT>
        EntityT& GetComponent() const
        {
            return m_Registry->get<EntityT>( m_Handle );
        }

        bool operator==( const Entity& other )
        {
            return m_Handle == other.m_Handle;
        }

        const auto GetHandle() const
        {
            return m_Handle;
        }

    private:
        entt::entity    m_Handle;
        entt::registry* m_Registry;
    };
} // namespace Desert::ECS