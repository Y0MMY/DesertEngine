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
            using UniformBufferPair = std::pair<Core::Models::UniformBuffer, std::vector<VkWriteDescriptorSet>>;
            using SamplerBufferPair = std::pair<Core::Models::ImageSampler, std::vector<VkWriteDescriptorSet>>;
            using StorageBufferPair = std::pair<Core::Models::StorageBuffer, std::vector<VkWriteDescriptorSet>>;

            using UniformBufferMap = std::unordered_map<BindingPoint, UniformBufferPair>;
            using ImageSamplerMap  = std::unordered_map<BindingPoint, SamplerBufferPair>;
            using StorageBufferMap = std::unordered_map<BindingPoint, StorageBufferPair>;

            UniformBufferMap UniformBuffers;
            ImageSamplerMap  ImageSamplers;
            StorageBufferMap StorageBuffers;
            ImageSamplerMap  StorageImageSamplers;

            operator bool()
            {
                return !UniformBuffers.empty();
            }
        };

    } // namespace ShaderResource
} // namespace Desert::Graphic::API::Vulkan