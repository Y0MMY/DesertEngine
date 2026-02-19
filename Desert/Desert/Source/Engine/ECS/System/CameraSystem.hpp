#pragma once

#include "System.hpp"

#include <Engine/ECS/Components.hpp>

namespace Desert::ECS
{
    class CameraSystem : public System
    {
    public:
        using System::System;

        void Update( entt::registry& registry, const Common::Timestep& ts ) override
        {
            
        }

    };
} // namespace Desert::ECS