#pragma once

#include <glm/glm.hpp>

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>

#include <Engine/Core/Camera.hpp>

#include <Engine/Graphic/Materials/MaterialReflection.hpp>

namespace Desert::Graphic::Models
{
    // clang-format off
    RFL_UB_TYPE(CameraDataUB, "Camera",
        FIELD(glm::mat4, Projection, "Projection")
        FIELD(glm::mat4, View, "View")
        FIELD(glm::vec3, CameraPos, "Camera Pos"))
    // clang-format on

} // namespace Desert::Graphic::Models