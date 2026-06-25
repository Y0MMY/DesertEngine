#pragma once

#include "Image.hpp"
#include <Engine/Runtime/ImageHandle.hpp>

#include <glm/glm.hpp>

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

        static std::shared_ptr<Image2D>   ProccessForImage2D( const std::shared_ptr<Image>& image );
        // Bakes the procedural atmosphere into an equirect HDR panorama (RGBA32F, Storage|Sample). No
        // input image — the sky is generated in-shader from the sun direction/intensity/disk radius.
        static std::shared_ptr<Image2D>   BakeProceduralPanorama( uint32_t width, uint32_t height,
                                                                  const glm::vec3& sunDir, float intensity,
                                                                  float diskRadius );
        // Single dispatch: samples spec.InputHandle (2D panorama OR source cubemap) -> a fresh output cube.
        static std::shared_ptr<ImageCube> ProccessForImageCube( const ComputeImagesSpecification& spec );
        // GGX prefilter: convolves spec.InputHandle (radiance cube) per mip (roughness = mip/(mips-1))
        // into a mipped output cube. Returns the prefiltered cube.
        static std::shared_ptr<ImageCube> ProccessForImageCubeMips( const ComputeImagesSpecification& spec );
    };

} // namespace Desert::Graphic