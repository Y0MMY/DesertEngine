#pragma once

#include <Engine/Graphic/Models/DirectionLight.hpp>
#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapperUniform.hpp>

namespace Desert::Graphic::Models::Light
{
    DEFINE_MATERIAL_WRAPPER_UNIFORM( DirectionLightUB, std::vector<DirectionLight>, "LightningUB" );

} // namespace Desert::Graphic::Models::Light