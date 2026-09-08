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
            if ( !IsDirty() )
                return;

            // NO `if ( m_Texture )` HERE ANY MORE — that guard was the Г14 defect, one link before the
            // descriptor. A slot told "nothing" marked itself dirty, skipped the write and then marked
            // itself clean, so the descriptor kept the last cube ANY scene had given it. Both ends looked
            // right (the producer restates absence every frame; the descriptor is always defined) and the
            // middle link dropped it: CornellDemo -> Clouds_Protocol -> CornellDemo came back lit by the
            // cloud scene's sky. `UniformImageCube::SetImageCube( nullptr )` is what "nothing" MEANS, and
            // it is a write like any other.
            m_Uniform->SetImageCube( m_Texture );
            backend->ApplyTextureCube( this );
            MarkClean();
        }

        void SetTexture( const ImageCube* texture )
        {
            m_Texture = texture;
            MarkDirty(); // every slot owes itself this write — INCLUDING the one that clears it
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