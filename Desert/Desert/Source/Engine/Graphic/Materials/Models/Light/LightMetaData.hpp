#pragma once

#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapperUniform.hpp>

#include <Engine/Graphic/Materials/MaterialReflection.hpp>

namespace Desert::Graphic::Models::Light
{
    // clang-format off
    RFL_UB_TYPE(LightsMetadata,
        FIELD_VALUEUINT(DirectionLightCount, "LightsMetadata")
        FIELD_VALUEUINT(PointLightCount, "PointLightCount"))
    // clang-format on

    DEFINE_MATERIAL_WRAPPER_UNIFORM( LightsMetadataUB, LightsMetadata, "LightsMetadata" );
} // namespace Desert::Graphic::Models::Light