#pragma once

#include <Engine/Graphic/Models/DirectionLight.hpp>
#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapperUniform.hpp>

namespace Desert::Graphic::Models::Light
{
    // clang-format off
    RFL_UB_TYPE(DirectionLightsUB,
        FIELD(std::vector<DirectionLight>, directionLights, "Direction Lights");

        FIELD_ATTR(glm::vec3, ambientColor, "Ambient Color",
            .color(true, false)
            .description("Global ambient lighting")
            .category("Lighting"))

        FIELD_ATTR(float, intensity, "Light Intensity",
            .range(0.0f, 10.0f, 0.1f)
            .category("Lighting")
            .description("Overall light intensity multiplier"))
        )
    // clang-format on

    DEFINE_MATERIAL_WRAPPER_UNIFORM( DirectionLightUB, DirectionLightsUB, "LightningUB" );

} // namespace Desert::Graphic::Models::Light