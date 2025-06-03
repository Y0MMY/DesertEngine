#include <Engine/ECS/Entity.hpp>

#include <Engine/Core/Scene.hpp>

namespace Desert::ECS
{

    Entity::Entity( std::string&& tag, entt::entity handle, const std::reference_wrapper<Core::Scene>& scene )
         : m_Handle( handle ), m_Scene( scene )
    {
        AddComponent<TagComponent>( std::move( tag ) );
    }

} // namespace Desert::ECS