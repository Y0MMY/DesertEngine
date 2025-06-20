#pragma once

#include <glm/glm.hpp>

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Materials/MaterialWrapperTexture2D.hpp>

namespace Desert::Graphic::Models
{

    class ToneMap final : public MaterialHelper::MaterialWrapperTexture2D
    {
    public:
        explicit ToneMap( const std::shared_ptr<Material>&                 material,
                          const std::shared_ptr<Uniforms::UniformImage2D>& uniform )
             : MaterialHelper::MaterialWrapperTexture2D( material, uniform )
        {
        }

        void UpdateToneMap( const std::shared_ptr<Image2D>& toneMapImage )
        {
            m_ToneMapImage = toneMapImage;
            m_Uniform->SetImage2D( m_ToneMapImage );
        }

    private:
        std::shared_ptr<Image2D> m_ToneMapImage;
    };
} // namespace Desert::Graphic::Models