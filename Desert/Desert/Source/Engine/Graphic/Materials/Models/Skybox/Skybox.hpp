#pragma once

#include <glm/glm.hpp>

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Materials/MaterialWrapperTextureCube.hpp>

namespace Desert::Graphic::Models
{

    class SkyboxData final : public MaterialHelper::MaterialWrapperTextureCube
    {
    public:
        explicit SkyboxData( const std::shared_ptr<Uniforms::UniformImageCube>& uniform,
                             const std::shared_ptr<Material>&                   material )
             : MaterialHelper::MaterialWrapperTextureCube( material, uniform )
        {
        }

        void UpdateSkybox( const std::shared_ptr<ImageCube>& skyboxImage )
        {
            m_SkyboxImage = skyboxImage;
            m_Uniform->SetImageCube( m_SkyboxImage );
            Bind();
        }
    private:
        std::shared_ptr<ImageCube> m_SkyboxImage;
    };
} // namespace Desert::Graphic::Models