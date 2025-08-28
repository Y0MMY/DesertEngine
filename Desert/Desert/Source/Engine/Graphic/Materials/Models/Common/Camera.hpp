#pragma once

#include <glm/glm.hpp>

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>
#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapper.hpp>

#include <Engine/Core/Camera.hpp>

namespace Desert::Graphic::Models
{
    struct CameraDataUB
    {
        glm::mat4 Projection;
        glm::mat4 View;
        glm::vec3 CameraPos;
    };

    class CameraData final : public MaterialHelper::MaterialWrapper<CameraDataUB>
    {
    public:
        explicit CameraData( const std::shared_ptr<MaterialExecutor>& material, std::string&& ubName )
             : MaterialWrapper( material, std::move( ubName ) )
        {
        }

    private:
    };
} // namespace Desert::Graphic::Models