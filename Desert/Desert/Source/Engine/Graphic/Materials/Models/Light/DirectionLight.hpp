#pragma once

#include <glm/glm.hpp>
#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapper.hpp>

namespace Desert::Graphic::Models::Light
{
    DEFINE_MATERIAL_WRAPPER( DirectionLightUB, glm::vec3, "LightningUB" );

} // namespace Desert::Graphic::Models::Light