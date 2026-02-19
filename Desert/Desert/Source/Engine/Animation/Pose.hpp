#pragma once

#include <glm/glm.hpp>
#include <vector>

namespace Desert::Animation
{
    struct Pose
    {
        std::vector<glm::mat4> BoneMatrices;
    };
} // namespace Desert::Animation
