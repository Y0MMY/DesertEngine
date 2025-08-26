#pragma once

#include <glm/glm.hpp>

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>
#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapper.hpp>

#include <Engine/Core/Camera.hpp>

namespace Desert::Graphic::Models
{
    class CameraData final : public MaterialHelper::MaterialWrapper
    {
    public:
        struct CameraDataUB
        {
            glm::mat4 Projection;
            glm::mat4 View;
            glm::vec3 CameraPos;
        };

        explicit CameraData( const std::shared_ptr<MaterialExecutor>& material, std::string&& ubName )
             : MaterialHelper::MaterialWrapper( material, std::move( ubName ) )
        {
        }

        void UpdateCameraUB( const Core::Camera& camera )
        {
            CameraDataUB cameraObj;

            cameraObj.Projection = camera.GetProjectionMatrix();
            cameraObj.View       = camera.GetViewMatrix();
            cameraObj.CameraPos  = camera.GetPosition();

            m_UniformProperty->SetData( &cameraObj, 128 );
        }

    private:
    };
} // namespace Desert::Graphic::Models