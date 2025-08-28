#pragma once

#include <Common/Core/Timestep.hpp>
#include <Engine/ECS/Components.hpp>

#include "DirectionLight.hpp"

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    struct SceneRendererUpdate
    {
        Common::Timestep            Timestep;
        std::vector<DirectionLight> DirLights;
    };
} // namespace Desert::Graphic::Models