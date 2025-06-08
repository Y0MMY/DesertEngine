#pragma once

#include <Common/Core/Result.hpp>

namespace Desert::Graphic
{
    class Image2D;
    class ImageCube;

    enum class MipGenStrategy : uint8_t
    {
        ComputeShader,
        TransferOps
    };

    class MipMap2DGenerator
    {
    public:
        virtual Common::BoolResult GenerateMips( const std::shared_ptr<Image2D>& image ) const = 0;

        static std::unique_ptr<MipMap2DGenerator> Create( MipGenStrategy strategy );
    };

    class MipMapCubeGenerator
    {
    public:
        virtual Common::BoolResult GenerateMips( const std::shared_ptr<ImageCube>& image ) const = 0;

        static std::unique_ptr<MipMapCubeGenerator> Create( MipGenStrategy strategy );
    };
} // namespace Desert::Graphic