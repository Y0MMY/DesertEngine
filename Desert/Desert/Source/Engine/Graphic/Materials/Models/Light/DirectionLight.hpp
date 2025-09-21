#pragma once

#include <Engine/Graphic/Models/DirectionLight.hpp>
#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapperUniform.hpp>

namespace Desert::Graphic::Models::Light
{
    // clang-format off
    RFL_UB_TYPE( DirectionLightPropertiesUB, 
        FIELD( DirectionLights, std::vector<DirectionLight> ) )
    // clang-format on

    DEFINE_MATERIAL_WRAPPER_UNIFORM( DirectionLightUB, DirectionLightPropertiesUB, "LightningUB" );

} // namespace Desert::Graphic::Models::Light