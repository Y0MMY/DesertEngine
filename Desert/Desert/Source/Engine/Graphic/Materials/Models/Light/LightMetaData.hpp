#pragma once

#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapperUniform.hpp>

namespace Desert::Graphic::Models::Light
{
    struct LightsMetadata
    {
        uint32_t DirectionLightCount;
        uint32_t PointLightCount;
    };

    DEFINE_MATERIAL_WRAPPER_UNIFORM( LightsMetadataUB, LightsMetadata, "LightsMetadata" );
} // namespace Desert::Graphic::Models::Light