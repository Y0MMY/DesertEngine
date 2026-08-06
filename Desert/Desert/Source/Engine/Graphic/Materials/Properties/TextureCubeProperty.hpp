#pragma once

#include <Engine/Graphic/Materials/Properties/MaterialProperty.hpp>

#include <Engine/ShaderResources/UniformImageCube.hpp>

namespace Desert::Graphic
{
    class TextureCubeProperty : public MaterialProperty
    {
    public:
        TextureCubeProperty( std::shared_ptr<ShaderResources::UniformImageCube> uniform ) : m_Uniform( uniform )
        {
        }

        void Apply( MaterialBackend* backend ) override
        {
            if ( IsDirty() )
            {
                if ( m_Texture )
                {
                    m_Uniform->SetImageCube( m_Texture );
                    backend->ApplyTextureCube( this );
                }
                MarkClean();
            }
        }

        std::unique_ptr<MaterialProperty> Clone() const override
        {
            /*auto prop = std::make_unique<TextureCubeProperty>( m_Uniform );
            prop->SetTexture( m_Texture );
            return prop;*/

            return nullptr;
        }

        void SetTexture( const ImageCube* texture )
        {
            m_Texture = texture;
            MarkDirty(); // every slot owes itself this write
        }

        const auto& GetUniform() const
        {
            return m_Uniform;
        }

    private:
        std::shared_ptr<ShaderResources::UniformImageCube> m_Uniform;
        const ImageCube*                            m_Texture = nullptr;
    };
} // namespace Desert::Graphic