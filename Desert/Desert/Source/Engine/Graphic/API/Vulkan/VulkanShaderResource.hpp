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
            std::unordered_map<uint32_t, Core::Models::UniformBuffer>           UniformBuffers;
            std::unordered_map<std::string, VkWriteDescriptorSet> WriteDescriptorSets;

            operator bool()
            {
                return !UniformBuffers.empty();
            }
        };

    } // namespace ShaderResource
} // namespace Desert::Graphic::API::Vulkan