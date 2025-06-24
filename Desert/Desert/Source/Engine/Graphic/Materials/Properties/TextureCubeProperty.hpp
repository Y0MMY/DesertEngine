#pragma once

#include <Engine/Graphic/Materials/Properties/MaterialProperty.hpp>

#include <Engine/Uniforms/UniformImageCube.hpp>

namespace Desert::Graphic
{
    class TextureCubeProperty : public MaterialProperty
    {
    public:
        TextureCubeProperty( std::shared_ptr<Uniforms::UniformImageCube> uniform ) : m_Uniform( uniform )
        {
        }

        void Apply( MaterialBackend* backend ) override
        {
            if ( m_Texture )
            {
                m_Uniform->SetImageCube( m_Texture );
            }

            backend->ApplyTexture2D( this );
        }

        std::unique_ptr<MaterialProperty> Clone() const override
        {
            /*auto prop = std::make_unique<TextureCubeProperty>( m_Uniform );
            prop->SetTexture( m_Texture );
            return prop;*/

            return nullptr;
        }

        void SetTexture( std::shared_ptr<ImageCube> texture )
        {
            m_Texture = texture;
            m_Uniform->SetImageCube(m_Texture);
        }

        const auto& GetUniform() const
        {
            return m_Uniform;
        }

    private:
        std::shared_ptr<Uniforms::UniformImageCube> m_Uniform;
        std::shared_ptr<ImageCube>                  m_Texture;
    };
} // namespace Desert::Graphic