#pragma once

#include <glm/glm.hpp>
#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapper.hpp>

namespace Desert::Graphic::Models::PBR
{
    struct PBRMaterialPropertiesUB
    {
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

    DEFINE_MATERIAL_WRAPPER( MaterialPBRUB, PBRMaterialPropertiesUB, "MaterialProperties" )

} // namespace Desert::Graphic::Models::PBR