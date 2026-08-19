#pragma once

#include <Engine/Graphic/Materials/Material.hpp>

namespace Desert::Graphic
{
    // Binds the two images the cloud composite pass reads: the RGBA16F scatter image (premultiplied
    // radiance + transmittance) and the depth guide beside it (cloud front distance and scene distance,
    // kilometres), which the shader upsamples the first through. No parameters — the march already
    // resolved the lighting, and the pipeline's blend state does the rest.
    //
    // The pair comes from CloudTemporalResolve, not from the march itself: the march traces at a quarter
    // of the view and the reconstruction turns four such frames into the half-resolution pair this
    // composites. That is invisible here on purpose — the interface is "two half-res images of the same
    // size", and it did not change when the producer behind it did.
    class MaterialCloudComposite final : public Material
    {
    public:
        MaterialCloudComposite();

        // Both images are required and are always the same size. They are passed together rather than
        // through two calls because the shader indexes them with ONE set of coordinates: a frame in which
        // only one of them was refreshed would read this frame's radiance against last frame's edges.
        void Bind( const Image2D* scatterImage, const Image2D* guideImage );

    private:
        Texture2DProperty* m_ScatterTexture = nullptr;
        Texture2DProperty* m_GuideTexture   = nullptr;
    };
} // namespace Desert::Graphic
