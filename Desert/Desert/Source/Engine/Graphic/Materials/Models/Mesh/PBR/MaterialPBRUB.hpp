#pragma once

#include <glm/glm.hpp>
#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapperUniform.hpp>

#include <Engine/Graphic/Materials/MaterialReflection.hpp>

namespace Desert::Graphic::Models::PBR
{
    /* struct PBRMaterialPropertiesUB
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
     };*/

    // clang-format off
    RFL_UB_TYPE( PBRMaterialPropertiesUB,
        FIELD(glm::vec3, AlbedoColor, "Albedo Color" )
        FIELD(float, AlbedoBlend, "Albedo Blend" ) )
    // clang-format on

    DEFINE_MATERIAL_WRAPPER_UNIFORM( MaterialPBRUB, PBRMaterialPropertiesUB, "MaterialProperties" )

} // namespace Desert::Graphic::Models::PBR