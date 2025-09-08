#pragma once

#include <glm/glm.hpp>
#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapperUniform.hpp>
#include <Engine/Graphic/Models/PointLight.hpp>

namespace Desert::Graphic::Models::Light
{
    DEFINE_MATERIAL_WRAPPER_UNIFORM( PointLightUB, std::vector<PointLight>, "PointLights" );
} // namespace Desert::Graphic::Models::Light