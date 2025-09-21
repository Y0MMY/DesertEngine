#pragma once

#include <glm/glm.hpp>
#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapperUniform.hpp>

#include <string>
#include <unordered_map>
#include <functional>
#include <glm/glm.hpp>

namespace Desert::Graphic::Models::PBR
{
    // clang-format off
    RFL_UB_TYPE( PBRMaterialPropertiesUB,
        FIELD( AlbedoColor, glm::vec3 )
        FIELD( AlbedoBlend, float ) 
        FIELD( MetallicValue, float )
        FIELD( MetallicBlend, float ) 
        FIELD( RoughnessValue, float ) 
        FIELD( RoughnessBlend, float )
        FIELD( EmissionColor, glm::vec3 ) 
        FIELD( EmissionStrength, float )
        FIELD( AOValue, float ) )
    // clang-format on

    DEFINE_MATERIAL_WRAPPER_UNIFORM( MaterialPBRUB, PBRMaterialPropertiesUB, "MaterialProperties" )

} // namespace Desert::Graphic::Models::PBR