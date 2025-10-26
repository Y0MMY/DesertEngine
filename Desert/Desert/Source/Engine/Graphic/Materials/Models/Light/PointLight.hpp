#pragma once

#include <glm/glm.hpp>
#include <Engine/Graphic/Models/PointLight.hpp>

namespace Desert::Graphic::Models::Light
{
    // clang-format off
    RFL_UB_TYPE(PointLightsUB, "PointLights",
        FIELD(std::vector<PointLight>, lights, "lights"))
    // clang-format on

} // namespace Desert::Graphic::Models::Light