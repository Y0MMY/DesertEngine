#include <Engine/ECS/Entity.hpp>

#include <Engine/Core/Scene.hpp>

namespace Desert::ECS
{

    Entity::Entity( std::string&& tag, entt::entity handle, Common::Memory::not_null<Core::Scene>&& scene )
         : m_Handle( handle ), m_Scene( std::move( scene ) )
    {
        AddComponent<TagComponent>( std::move( tag ) );
        AddComponent<UUIDComponent>();
        AddComponent<TransformComponent>();
    }



} // namespace Desert::ECS