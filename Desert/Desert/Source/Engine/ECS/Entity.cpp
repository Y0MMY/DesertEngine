#include <Engine/ECS/Entity.hpp>

namespace Desert::ECS
{

    Entity::Entity( std::string&& name, entt::entity handle) : m_Name( std::move( name ) ), m_Handle(handle)
    {
    }

} // namespace Desert