#pragma once

#include <vulkan/vulkan.h>

namespace Desert::Graphic::API::Vulkan
{
    namespace ShaderResource
    {
        struct UniformBuffer
        {
            VkDescriptorBufferInfo Descriptor;
            uint32_t               Size         = 0;
            uint32_t               BindingPoint = 0;
            std::string            Name;
            VkShaderStageFlagBits  ShaderStage = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
        };

        struct ShaderDescriptorSet
        {
            std::unordered_map<uint32_t, UniformBuffer> UniformBuffers;
        };

    } // namespace ShaderResource
} // namespace Desert::Graphic::API::Vulkan