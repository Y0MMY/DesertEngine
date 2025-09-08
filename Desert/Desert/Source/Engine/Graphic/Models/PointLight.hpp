#pragma once

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    struct PointLight
    {
        glm::vec3 Color;
        float     Intensity; // 16 bytes offset
        glm::vec3 Position;
        float     Radius; // 32 bytes offset
    };
} // namespace Desert::Graphic