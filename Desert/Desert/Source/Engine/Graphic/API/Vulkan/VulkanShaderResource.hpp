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
            std::unordered_map<BindingPoint, Core::Models::UniformBuffer> UniformBuffers;
            std::unordered_map<uint32_t, Core::Models::ImageSampler> ImageSamplers;
            std::unordered_map<std::string, std::vector<VkWriteDescriptorSet>>
                 WriteDescriptorSets; // NOTE: std::vector defines the number of frames, i.e. there will be a
                                      // different VkWriteDescriptorSet for each frame

            operator bool()
            {
                return !UniformBuffers.empty();
            }
        };

    } // namespace ShaderResource
} // namespace Desert::Graphic::API::Vulkan