#pragma once

#include <glm/glm.hpp>

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>
#include <Engine/Graphic/Materials/Models/Wrapper/MaterialWrapperTexture2D.hpp>

namespace Desert::Graphic::Models
{
    class ToneMap final : public MaterialHelper::MaterialWrapperTexture2D
    {
    public:
        explicit ToneMap( const std::shared_ptr<MaterialExecutor>& material )
             : MaterialHelper::MaterialWrapperTexture2D( material, "u_GeometryTexture")
        {
        }

        void UpdateToneMap( const std::shared_ptr<Image2D>& toneMapImage )
        {
            m_ToneMapImage = toneMapImage;
            m_UniformProperty->SetImage( m_ToneMapImage );
        }

    private:
        std::shared_ptr<Image2D> m_ToneMapImage;
    };
} // namespace Desert::Graphic::Models