#pragma once

#include <vulkan/vulkan.h>

#include <Engine/ShaderResources/ShaderReflectionTypes.hpp>

namespace Desert::Graphic::API::Vulkan
{
    namespace ShaderResource
    {

        inline constexpr uint32_t MAX_SETS = 1;

        struct ShaderDescriptorSet
        {
            using UniformBufferMap =
                 std::unordered_map<BindingPoint, ShaderResources::ShaderLayout::UniformBuffer>;
            using ImageSampler2DMap =
                 std::unordered_map<BindingPoint, ShaderResources::ShaderLayout::Image2DSampler>;
            using ImageSamplerCubeMap =
                 std::unordered_map<BindingPoint, ShaderResources::ShaderLayout::ImageCubeSampler>;
            using StorageBufferMap =
                 std::unordered_map<BindingPoint, ShaderResources::ShaderLayout::StorageBuffer>;

            UniformBufferMap    UniformBuffers;
            ImageSampler2DMap   Image2DSamplers;
            ImageSamplerCubeMap ImageCubeSamplers;
            StorageBufferMap    StorageBuffers;
            ImageSampler2DMap   StorageImage2DSamplers;

            operator bool()
            {
                return !UniformBuffers.empty();
            }
        };

    } // namespace ShaderResource
} // namespace Desert::Graphic::API::Vulkan