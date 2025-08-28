#pragma once

#include <glm/glm.hpp>
#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapper.hpp>

namespace Desert::Graphic::Models::Light
{
    struct PointLightMaterialUB
    {
        glm::vec3 Color;
        glm::vec3 Position;
        float     Intensity;
        float     Radius;
    };

    class PointLightUB final : public MaterialHelper::MaterialWrapper<PointLightMaterialUB>
    {
    public:
        explicit PointLightUB( const std::shared_ptr<MaterialExecutor>& material )
             : MaterialWrapper( material, "PointLights" )
        {
        }
    };
    ;
} // namespace Desert::Graphic::Models::Light