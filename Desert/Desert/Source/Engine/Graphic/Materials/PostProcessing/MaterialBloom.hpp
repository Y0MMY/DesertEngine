#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Image.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    // Bloom bright-pass: extracts pixels above `threshold` (push constant) from the input image.
    class MaterialBloomBright final : public Material
    {
    public:
        MaterialBloomBright();

        void Bind( const Image2D* input, float threshold );

    private:
        Texture2DProperty* m_InputTexture = nullptr;
    };

    // One separable-Gaussian blur pass. `direction` (texels) is a push constant. A SEPARATE instance is
    // used per pass (the bound input texture differs) to avoid intra-frame descriptor aliasing — same
    // reason MaterialJFAStep is instanced per step.
    class MaterialBloomBlur final : public Material
    {
    public:
        MaterialBloomBlur();

        void Bind( const Image2D* input, const glm::vec2& direction );

    private:
        Texture2DProperty* m_InputTexture = nullptr;
    };
} // namespace Desert::Graphic
