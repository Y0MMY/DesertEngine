#pragma once

#include <Engine/Graphic/Models/DirectionLight.hpp>
#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapperUniform.hpp>

namespace Desert::Graphic::Models::Light
{
    // clang-format off
    RFL_UB_TYPE(DirectionLightsUB,
        FIELD(std::vector<DirectionLight>, directionLights, "directionLights"))
    // clang-format on

    DEFINE_MATERIAL_WRAPPER_UNIFORM( DirectionLightUB, DirectionLightsUB, "LightningUB" );

} // namespace Desert::Graphic::Models::Light