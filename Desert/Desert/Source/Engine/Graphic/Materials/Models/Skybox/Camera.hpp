#pragma once

#include <glm/glm.hpp>

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Materials/MaterialWrapper.hpp>

#include <Engine/Core/Camera.hpp>

namespace Desert::Graphic::Models
{
    class CameraData final : public MaterialHelper::MaterialWrapper
    {
    public:
        explicit CameraData( const std::shared_ptr<Material>& material )
             : MaterialHelper::MaterialWrapper( material, "camera")
        {
        }

        void UpdateCameraUB( const Core::Camera& camera )
        {
            struct
            {
                glm::mat4 m1;
                glm::mat4 m2;
            } cameraObj;

            cameraObj.m1 = camera.GetProjectionMatrix();
            cameraObj.m2 = camera.GetViewMatrix();

            m_UniformProperty->SetData( &cameraObj, 128 );
        }

    private:
    };
} // namespace Desert::Graphic::Models