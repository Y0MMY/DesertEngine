#pragma once

#include <Engine/Graphic/Materials/Material.hpp>

namespace Desert::Graphic
{
    // Binds the two images the volumetric-cloud composite magnifies: the resolved cloud colour and the
    // depth guide its bilateral filter weights by. No parameters — the raymarch already produced
    // premultiplied radiance and transmittance, the blend state does the rest, and the filter's tolerance
    // is a property of the guide's encoding rather than a look control.
    class MaterialVolumetricClouds final : public Material
    {
    public:
        MaterialVolumetricClouds();

        /**
         * @param resolvedImage the image the composite samples: the temporal resolve's output, or the
         *                      raymarch target itself when Temporal Mode is Off. The composite does not
         *                      distinguish them — both carry premultiplied radiance and transmittance.
         * @param depthGuide    the raymarch's per-texel record of how far each ray was allowed to run.
         */
        void Bind( const Image2D* resolvedImage, const Image2D* depthGuide );

    private:
        Texture2DProperty* m_ResolvedTexture   = nullptr;
        Texture2DProperty* m_DepthGuideTexture = nullptr;
    };
} // namespace Desert::Graphic
