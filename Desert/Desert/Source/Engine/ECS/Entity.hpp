#pragma once

#include <Engine/ECS/Components.hpp>
#include <Engine/Core/Scene.hpp>

#include <Common/Core/Memory/not_null.hpp>

namespace Desert::Core
{
    class Scene;
}

namespace Desert::ECS
{
    class Entity final
    {
    public:
        explicit Entity( std::string&& tag, entt::entity handle, Common::Memory::not_null<Core::Scene>&& scene );
        ~Entity() = default;

        template <typename EntityT>
        bool HasComponent() const
        {
            return m_Scene->GetRegistry().has<EntityT>( m_Handle );
        }

        template <typename EntityT, typename... Args>
        decltype( auto ) AddComponent( Args&&... args ) const
        {
            return m_Scene->GetRegistry().emplace<EntityT>( m_Handle, std::forward<Args>( args )... );
        }

        template <typename EntityT>
        EntityT& GetComponent() const
        {
            return m_Scene->GetRegistry().get<EntityT>( m_Handle );
        }

        bool operator==( const Entity& other )
        {
            return m_Scene.get() == other.m_Scene.get() && m_Handle == other.m_Handle;
        }

        const auto GetHandle() const
        {
            return m_Handle;
        }

    private:
        entt::entity                                m_Handle;
        const Common::Memory::not_null<Core::Scene> m_Scene;
    };
} // namespace Desert::ECS