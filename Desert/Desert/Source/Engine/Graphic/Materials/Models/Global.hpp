#pragma once

#include <glm/glm.hpp>

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>
#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapper.hpp>

namespace Desert::Graphic::Models
{
    struct GlobalUB
    {
        glm::vec3 CameraPosition;
    };

    class GlobalData final : public MaterialHelper::MaterialWrapper<GlobalUB>
    {
    public:
        explicit GlobalData( const std::shared_ptr<MaterialExecutor>& material )
             : MaterialWrapper( material, "GlobalUB" )
        {
        }
    };
} // namespace Desert::Graphic::Models