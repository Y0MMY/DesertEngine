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
        using MaterialHelper::MaterialWrapper::MaterialWrapper;

        void UpdateCameraUB( const Core::Camera& camera )
        {
            struct
            {
                glm::mat4 m1;
                glm::mat4 m2;
            } cameraObj;

            cameraObj.m1 = camera.GetProjectionMatrix();
            cameraObj.m2 = camera.GetViewMatrix();

            m_Uniform->RT_SetData( &cameraObj, 128, 0U );
        }

    private:
    };
} // namespace Desert::Graphic::Models