#pragma once

#include "Image.hpp"

namespace Desert::Graphic
{
    struct ComputeImagesSpecification
    {
        std::shared_ptr<Image> InputImage;
        std::string            Tag;
        std::string            ShaderName;
        uint32_t               MipLevels;
        uint32_t               Width;
        uint32_t               Height;
    };

    class ComputeImages final
    {
    public:
        virtual ~ComputeImages() = default;

        static std::shared_ptr<Image2D>   ProccessForImage2D( const std::shared_ptr<Image>& image );
        static std::shared_ptr<ImageCube> ProccessForImageCube( const ComputeImagesSpecification& spec );
        static void                       ProccessForImageCubeMips( const ComputeImagesSpecification& spec );
    };

} // namespace Desert::Graphic