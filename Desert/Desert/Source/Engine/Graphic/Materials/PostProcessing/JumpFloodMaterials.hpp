#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Image.hpp>

namespace Desert::Graphic
{
    // Converts the silhouette mask into the initial Jump Flood seed texture.
    // Each masked pixel stores its own coordinates as a seed; everything else stores (-1, -1).
    class MaterialJFAInit final : public Material
    {
    public:
        MaterialJFAInit();

        void Bind( const Image2D* maskImage );

    private:
        Texture2DProperty* m_MaskTexture = nullptr;
    };

    // A single Jump Flood propagation step. Reads the previous seed texture and the current
    // sample distance (u_StepLength) and writes the nearest seed found within that neighbourhood.
    class MaterialJFAStep final : public Material
    {
    public:
        MaterialJFAStep();

        void Bind( const Image2D* inputSeed, int stepLength );

    private:
        Texture2DProperty* m_InputTexture = nullptr;
    };
} // namespace Desert::Graphic
