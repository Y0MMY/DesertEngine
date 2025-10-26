#pragma once

#include <Engine/Graphic/Materials/MaterialReflection.hpp>

namespace Desert::Graphic::Models::Light
{
    // clang-format off
    RFL_UB_TYPE(LightsMetadata, "LightsMetadata",
        FIELD(uint32_t, DirectionLightCount, "LightsMetadata")
        FIELD(uint32_t, PointLightCount, "PointLightCount"))
    // clang-format on

} // namespace Desert::Graphic::Models::Light