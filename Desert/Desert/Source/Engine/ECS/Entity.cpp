#include <Engine/ECS/Entity.hpp>

#include <Engine/Core/Scene.hpp>

namespace Desert::ECS
{

    Entity::Entity( std::string&& tag, entt::entity handle, entt::registry& registry )
         : m_Handle( handle ), m_Registry( &registry )
    {
        AddComponent<TagComponent>( std::move( tag ) );
        AddComponent<UUIDComponent>();
        AddComponent<TransformComponent>();
    }

} // namespace Desert::ECS