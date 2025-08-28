#pragma once

#include <Common/Core/Timestep.hpp>
#include <Engine/ECS/Components.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    struct DirectionLight
    {
        glm::vec3 Direction;
    };
} // namespace Desert::Graphic