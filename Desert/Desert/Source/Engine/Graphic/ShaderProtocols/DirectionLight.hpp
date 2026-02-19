#pragma once

#include <glm/glm.hpp>

namespace Desert::Graphic::ShaderProtocols
{
    struct DirectionLightPayload
    {
        glm::vec3 Direction;
    };

    struct DirectionLight
    {
        inline const static std::string Name = "DirectionLightUB";

        std::vector<DirectionLightPayload> DirectionLights;
    };

} // namespace Desert::Graphic::ShaderProtocols
