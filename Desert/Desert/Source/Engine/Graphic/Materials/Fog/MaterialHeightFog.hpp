#pragma once

#include <Engine/Graphic/Materials/Material.hpp>

namespace Desert::Graphic
{
    // Binds the one image the height-fog apply pass composites: the RGBA16F fog image the compute pass
    // wrote (premultiplied inscattering + transmittance). No parameters — the closed form already ran,
    // and the blend state does the rest. The MaterialVolumetricClouds arrangement, one sampler smaller.
    class MaterialHeightFog final : public Material
    {
    public:
        MaterialHeightFog();

        void Bind( const Image2D* fogImage );

    private:
        Texture2DProperty* m_FogTexture = nullptr;
    };
} // namespace Desert::Graphic
