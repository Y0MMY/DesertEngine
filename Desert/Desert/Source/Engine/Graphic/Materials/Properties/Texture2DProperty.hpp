#pragma once

#include <Engine/Graphic/Materials/Properties/MaterialProperty.hpp>

#include <Engine/Uniforms/UniformImage2D.hpp>

namespace Desert::Graphic
{
    class Texture2DProperty : public MaterialProperty
    {
    public:
        Texture2DProperty( std::shared_ptr<Uniforms::UniformImage2D> uniform ) : m_Uniform( uniform )
        {
        }

        void Apply( MaterialBackend* backend ) override
        {
            if ( m_Texture )
            {
                m_Uniform->SetImage2D( m_Texture );
            }

            backend->ApplyTexture2D( this );
        }

        std::unique_ptr<MaterialProperty> Clone() const override
        {
            // auto prop = std::make_unique<Texture2DProperty>( m_Uniform );
            // prop->SetTexture( m_Texture );
            // return prop;

            return nullptr;
        }

        void SetTexture( std::shared_ptr<Image2D> texture )
        {
            m_Texture = texture;
            m_Uniform->SetImage2D( m_Texture );
        }

        const auto& GetUniform() const
        {
            return m_Uniform;
        }

    private:
        std::shared_ptr<Uniforms::UniformImage2D> m_Uniform;
        std::shared_ptr<Image2D>                  m_Texture;
    };
} // namespace Desert::Graphic