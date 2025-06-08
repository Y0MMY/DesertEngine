#pragma once

#include <Engine/Graphic/MipMapGenerator.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanMipMap2DGeneratorCS : public MipMap2DGenerator
    {
    public:
        virtual Common::BoolResult GenerateMips( const std::shared_ptr<Image2D>& image ) const override;
    };

    class VulkanMipMapCubeGeneratorCS : public MipMapCubeGenerator
    {
    public:
        virtual Common::BoolResult GenerateMips( const std::shared_ptr<ImageCube>& imageCube) const override;
    };
} // namespace Desert::Graphic::API::Vulkan