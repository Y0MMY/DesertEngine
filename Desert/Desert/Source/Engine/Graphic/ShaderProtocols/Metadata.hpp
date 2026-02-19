#pragma once

#include <glm/glm.hpp>

namespace Desert::Graphic::ShaderProtocols
{
    struct LightsMetadata
    {
        inline const static std::string Name = "LightsMetadata";

        uint32_t DirectionLightsCount = 0U;
        uint32_t PointLightsCount     = 0U;
    };
} // namespace Desert::Graphic::ShaderProtocols
