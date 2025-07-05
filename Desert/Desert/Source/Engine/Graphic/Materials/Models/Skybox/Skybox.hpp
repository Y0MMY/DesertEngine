#pragma once

#include <glm/glm.hpp>

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapperTextureCube.hpp>

namespace Desert::Graphic::Models
{

    class SkyboxData final : public MaterialHelper::MaterialWrapperTextureCube
    {
    public:
        explicit SkyboxData( const std::shared_ptr<Material>& material )
             : MaterialHelper::MaterialWrapperTextureCube( material, "samplerCubeMap" )
        {
        }

        void UpdateSkybox( const std::shared_ptr<ImageCube>& skyboxImage )
        {
            if ( !skyboxImage )
            {
                return;
            }

            m_SkyboxImage = skyboxImage;
            m_UniformProperty->SetTexture( m_SkyboxImage );
        }

    private:
        std::shared_ptr<ImageCube> m_SkyboxImage;
    };
} // namespace Desert::Graphic::Models