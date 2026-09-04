#pragma once

#include "Image.hpp"
#include <Engine/Graphic/Clouds/CloudEnvironmentBake.hpp>
#include <Engine/Runtime/ImageHandle.hpp>

#include <glm/glm.hpp>

namespace Desert::ShaderResources
{
    class StorageBuffer;
}

namespace Desert::Graphic
{
    struct ComputeImagesSpecification
    {
        Runtime::ImageHandle InputHandle;
        std::string          Tag;
        std::string          ShaderName;
        uint32_t             MipLevels;
        uint32_t             Width;
        uint32_t             Height;
    };

    class ComputeImages final
    {
    public:
        virtual ~ComputeImages() = default;

        static std::shared_ptr<Image2D> ProccessForImage2D( const std::shared_ptr<Image>& image );
        // Bakes the procedural atmosphere into an equirect HDR panorama (RGBA32F, Storage|Sample). No input
        // image: the sky is generated in-shader from @p skyParams, which is the SAME buffer the screen sky
        // pass reads, so the baked lighting and the visible sky cannot describe different skies.
        // @p transmittanceLut / @p multiScatterLut feed the physical model's march; nullptr (the gradient
        // model) binds the engine fallbacks so the shader's samplers are never undefined descriptors.
        // @p clouds is this view's cloud layer, marched into the same panorama by the same field the
        // screen pass marches — see Engine/Graphic/Clouds/CloudEnvironmentBake.hpp. Its Marched flag is
        // the only gate: every descriptor the shader declares is written on every path, because an
        // unbound one makes the set invalid and this backend answers an invalid set by skipping the whole
        // dispatch — which here would lose the environment, not the clouds, with nothing in the log.
        static std::shared_ptr<Image2D> BakeProceduralPanorama( uint32_t width, uint32_t height,
                                                                ShaderResources::StorageBuffer* skyParams,
                                                                Image2D*                        transmittanceLut,
                                                                Image2D*                        multiScatterLut,
                                                                const CloudBakeBinding&         clouds );
        // Single dispatch: samples spec.InputHandle (2D panorama OR source cubemap) -> a fresh output cube.
        static std::shared_ptr<ImageCube> ProccessForImageCube( const ComputeImagesSpecification& spec );
        // GGX prefilter: convolves spec.InputHandle (radiance cube) per mip (roughness = mip/(mips-1))
        // into a mipped output cube. Returns the prefiltered cube.
        static std::shared_ptr<ImageCube> ProccessForImageCubeMips( const ComputeImagesSpecification& spec );
    };

} // namespace Desert::Graphic