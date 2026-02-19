#pragma once

#include <glm/glm.hpp>

namespace Desert::Graphic::ShaderProtocols
{
    struct PBRMeshMaterialsUB
    {
        inline const static std::string Name = "PBRMaterialPropertiesUB";

        glm::vec3 AlbedoColor;
        float     AlbedoBlend;
        float     MetallicValue;
        float     MetallicBlend;
        float     RoughnessValue;
        float     RoughnessBlend;
        glm::vec3 EmissionColor;
        float     EmissionStrength;
        float     AOValue;
    };
} // namespace Desert::Graphic::ShaderProtocols
