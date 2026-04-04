#pragma once

#include <vector>
#include <string>
#include "AnimationTransition.hpp"

namespace Desert::Animation
{
    struct AnimationState
    {
        std::string Name;
        std::string ClipName;
        bool        Loop = true;

        std::vector<AnimationTransition> Transitions;
    };
} // namespace Desert::Animation