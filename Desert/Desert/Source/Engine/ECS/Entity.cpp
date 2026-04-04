#include <Engine/ECS/Entity.hpp>

#include <Engine/Core/Scene.hpp>

namespace Desert::ECS
{

    Entity::Entity( entt::entity handle, entt::registry& registry ) : m_Handle( handle ), m_Registry( &registry )
    {
    }

} // namespace Desert::ECS