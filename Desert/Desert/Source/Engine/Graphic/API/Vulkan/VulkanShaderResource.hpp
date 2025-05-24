#pragma once

#include <vulkan/vulkan.h>

#include <Engine/Core/Models/Shader.hpp>

namespace Desert::Graphic::API::Vulkan
{
    namespace ShaderResource
    {

        inline constexpr uint32_t MAX_SETS = 1;

        struct ShaderDescriptorSet
        {
            using UniformBufferMap    = std::unordered_map<BindingPoint, Core::Models::UniformBuffer>;
            using ImageSampler2DMap   = std::unordered_map<BindingPoint, Core::Models::Image2DSampler>;
            using ImageSamplerCubeMap = std::unordered_map<BindingPoint, Core::Models::ImageCubeSampler>;
            using StorageBufferMap    = std::unordered_map<BindingPoint, Core::Models::StorageBuffer>;

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