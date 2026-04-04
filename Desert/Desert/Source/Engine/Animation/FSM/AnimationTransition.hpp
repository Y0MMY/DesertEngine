#pragma once

#include <functional>
#include <string>

namespace Desert::Animation
{
    struct AnimationTransition
    {
        std::string TargetState;
        float       BlendDuration = 0.2f;

        std::function<bool()> Condition;
    };
} // namespace Desert::Animation