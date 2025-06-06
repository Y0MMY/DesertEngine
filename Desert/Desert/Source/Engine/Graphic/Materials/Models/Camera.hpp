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
        explicit CameraData( const std::shared_ptr<UniformBuffer>& uniform,
                             const std::shared_ptr<Material>&      material )
             : MaterialHelper::MaterialWrapper( material ), m_UniformBuffer( uniform )
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

            m_UniformBuffer->RT_SetData( &cameraObj, 128, 0U );
            m_Material->AddUniformToOverride( m_UniformBuffer );
        }

    private:
        const std::shared_ptr<UniformBuffer> m_UniformBuffer;
    };
} // namespace Desert::Graphic::Models