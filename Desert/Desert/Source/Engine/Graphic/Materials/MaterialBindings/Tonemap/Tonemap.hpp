#pragma once

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>

namespace Desert::Graphic::MaterialHelper
{
    class TonemapBinding final
    {
    public:
        explicit TonemapBinding( const MaterialExecutor* materialExecutor )
        {
            m_UniformProperty = materialExecutor->GetTexture2DProperty( "u_GeometryTexture" ).get();
        }

        void UpdateTexture( Image2D* skyboxImage )
        {
            if ( !skyboxImage )
            {
                return;
            }

            m_TonemapImage = skyboxImage;
            m_UniformProperty->SetImage( m_TonemapImage );
        }

    private:
        Texture2DProperty* m_UniformProperty = nullptr;
        Image2D*           m_TonemapImage    = nullptr;
    };
} // namespace Desert::Graphic::MaterialHelper