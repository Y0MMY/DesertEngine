#pragma once

#include <glm/glm.hpp>

namespace Desert::Editor::Render::Model
{
    // clang-format off
    RFL_UB_TYPE(GridMaterialPropertiesUB, "GridUniforms",
        FIELD(float, CellSize, "CellSize")
        FIELD(float, CellScale, "CellScale")
        )
    // clang-format on
} // namespace Desert::Editor::Render::Model