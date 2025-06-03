#pragma once

//#include <entt/entt.hpp>


namespace entt
{
    using entity = uint32_t;
}

namespace Desert::ECS
{
    class Entity final
    {
    public:
        explicit Entity( std::string&& name, entt::entity handle);
        explicit Entity( Entity&& other ) : m_Name( std::move( other.m_Name ) )
        {
        }

        Entity& operator=( Entity&& other )
        {
            if ( this != &other )
            {
                m_Name = std::move( other.m_Name );
            }
            return *this;
        }

    private:
        std::string m_Name;
        entt::entity m_Handle;
    };
} // namespace Desert::ECS