#pragma once

#include <glm/glm.hpp>

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>
#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapperUniform.hpp>

#include <Engine/Core/Camera.hpp>

#include <Engine/Graphic/Materials/MaterialReflection.hpp>

namespace Desert::Graphic::Models
{
    // clang-format off
    RFL_UB_TYPE(CameraDataUB,
        FIELD(glm::mat4, Projection, "Projection")
        FIELD(glm::mat4, View, "View")
        FIELD(glm::vec3, CameraPos, "Camera Pos"))
    // clang-format on

    class CameraData final : public MaterialHelper::MaterialWrapperUniform<CameraDataUB>
    {
    public:
        explicit CameraData( const std::shared_ptr<MaterialExecutor>& material, std::string&& ubName = "Camera" )
             : MaterialWrapperUniform( material, std::move( ubName ) )
        {
        }

    private:
    };
} // namespace Desert::Graphic::Models