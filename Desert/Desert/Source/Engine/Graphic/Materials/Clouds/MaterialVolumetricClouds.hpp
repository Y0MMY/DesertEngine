#pragma once

#include <Engine/Graphic/Materials/Material.hpp>

namespace Desert::Graphic
{
    // Binds the raymarch output for the volumetric-cloud composite. One texture and nothing else: the
    // composite has no parameters of its own, because the raymarch already produced premultiplied
    // radiance and transmittance and the blend state does the rest.
    class MaterialVolumetricClouds final : public Material
    {
    public:
        MaterialVolumetricClouds();

        void Bind( const Image2D* scatterImage );

    private:
        Texture2DProperty* m_ScatterTexture = nullptr;
    };
} // namespace Desert::Graphic
