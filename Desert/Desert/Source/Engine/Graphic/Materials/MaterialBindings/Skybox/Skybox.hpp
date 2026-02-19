#pragma once

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>

namespace Desert::Graphic::MaterialHelper
{
    class SkyboxDataBinding final
    {
    public:
        explicit SkyboxDataBinding( const MaterialExecutor* materialExecutor )
        {
            m_UniformProperty = materialExecutor->GetTextureCubeProperty( "samplerCubeMap" ).get();
        }

        void UpdateSkybox( ImageCube* skyboxImage )
        {
            if ( !skyboxImage )
            {
                return;
            }

            m_SkyboxImage = skyboxImage;
            m_UniformProperty->SetTexture( m_SkyboxImage );
        }

    private:
        TextureCubeProperty* m_UniformProperty = nullptr;
        ImageCube*           m_SkyboxImage = nullptr;
    };
} // namespace Desert::Graphic::MaterialHelper