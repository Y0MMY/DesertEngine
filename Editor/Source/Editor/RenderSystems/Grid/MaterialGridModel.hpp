#pragma once

#include <glm/glm.hpp>

#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapper.hpp>

namespace Desert::Editor::Render::Model
{
    struct GridMaterialPropertiesUB
    {
        float CellSize;
        float CellScale;
    };
    DEFINE_MATERIAL_WRAPPER(MaterialGridProperties, GridMaterialPropertiesUB, "GridUniforms")

} // namespace Desert::Editor::Render::Model