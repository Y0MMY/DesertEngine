#pragma once

#include <vulkan/vulkan.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class DescriptorSetBuilder
    {
    public:
        static VkWriteDescriptorSet GetUniformWDS( const std::shared_ptr<VulkanShader>& vulkanShader,
                                                   const uint32_t frame, const uint32_t set,
                                                   const uint32_t dstBinding, const uint32_t descriptorCount,
                                                   const VkDescriptorBufferInfo* pBufferInfo );

        static VkWriteDescriptorSet GetSamplerWDS( const std::shared_ptr<VulkanShader>& vulkanShader,
                                                   const uint32_t frame, const uint32_t set,
                                                   const uint32_t dstBinding, const uint32_t descriptorCount,
                                                   const VkDescriptorImageInfo* pImageInfo );

        static VkWriteDescriptorSet GetStorageWDS( const std::shared_ptr<VulkanShader>& vulkanShader,
                                                   const uint32_t frame, const uint32_t set,
                                                   const uint32_t dstBinding, const uint32_t descriptorCount,
                                                   const VkDescriptorImageInfo* pImageInfo );
    };
} // namespace Desert::Graphic::API::Vulkan