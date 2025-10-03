#pragma once

#include <glm/glm.hpp>
#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapperUniform.hpp>
#include <Engine/Graphic/Models/PointLight.hpp>

namespace Desert::Graphic::Models::Light
{
    // clang-format off
    RFL_UB_TYPE(PointLightsUB,
        FIELD(std::vector<PointLight>, lights, "lights"))
    // clang-format on

    DEFINE_MATERIAL_WRAPPER_UNIFORM( PointLightUB, PointLightsUB, "PointLights" );
} // namespace Desert::Graphic::Models::Light