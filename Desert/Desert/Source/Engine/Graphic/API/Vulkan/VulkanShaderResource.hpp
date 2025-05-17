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

            // NOTE: std::vector defines the number of frames, i.e. there will be a
            // different VkWriteDescriptorSet for each frame
            using UniformBufferPair   = std::pair<Core::Models::UniformBuffer, std::vector<VkWriteDescriptorSet>>;
            using Sampler2DBufferPair = std::pair<Core::Models::Image2DSampler, std::vector<VkWriteDescriptorSet>>;
            using SamplerCubeBufferPair =
                 std::pair<Core::Models::ImageCubeSampler, std::vector<VkWriteDescriptorSet>>;
            using StorageBufferPair = std::pair<Core::Models::StorageBuffer, std::vector<VkWriteDescriptorSet>>;

            using UniformBufferMap    = std::unordered_map<BindingPoint, UniformBufferPair>;
            using ImageSampler2DMap   = std::unordered_map<BindingPoint, Sampler2DBufferPair>;
            using ImageSamplerCubeMap = std::unordered_map<BindingPoint, SamplerCubeBufferPair>;
            using StorageBufferMap    = std::unordered_map<BindingPoint, StorageBufferPair>;

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