#pragma once

#include <glm/glm.hpp>

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>
#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapperUniform.hpp>

namespace Desert::Graphic::Models
{
    struct GlobalUB
    {
        glm::vec3 CameraPosition;
    };

    class GlobalData final : public MaterialHelper::MaterialWrapperUniform<GlobalUB>
    {
    public:
        explicit GlobalData( const std::shared_ptr<MaterialExecutor>& material )
             : MaterialWrapperUniform( material, "GlobalUB" )
        {
        }
    };
} // namespace Desert::Graphic::Models