#pragma once

#include <Engine/Graphic/Materials/Material.hpp>

namespace Desert::Graphic
{
    // Binds the input (tonemapped) image for the FXAA post-process pass.
    class MaterialFXAA final : public Material
    {
    public:
        MaterialFXAA();

        void Bind( const std::shared_ptr<Image2D>& inputImage );

    private:
        Texture2DProperty* m_InputTexture = nullptr;
    };
} // namespace Desert::Graphic
