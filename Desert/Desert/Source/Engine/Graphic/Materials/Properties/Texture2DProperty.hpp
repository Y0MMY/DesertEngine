#pragma once

#include <Engine/Graphic/Materials/Properties/MaterialProperty.hpp>

#include <Engine/Graphic/Texture.hpp>
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
            if ( m_Dirty )
            {
                if ( m_Texture )
                {
                    m_Uniform->SetImage2D( m_Texture );
                }
                backend->ApplyTexture2D( this );
                m_Dirty = false;
            }
        }

        std::unique_ptr<MaterialProperty> Clone() const override
        {
            // auto prop = std::make_unique<Texture2DProperty>( m_Uniform );
            // prop->SetTexture( m_Texture );
            // return prop;

            return nullptr;
        }

        void SetTexture( const std::shared_ptr<Texture2D>& texture )
        {
            if ( texture )
                m_Texture = texture->GetImage2D();
            m_Dirty = true;
        }

        void SetImage( std::shared_ptr<Image2D> texture )
        {
            m_Texture = texture;
            m_Dirty   = true;
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