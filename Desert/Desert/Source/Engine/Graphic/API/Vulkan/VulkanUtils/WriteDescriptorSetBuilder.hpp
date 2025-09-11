#pragma once

#include <vulkan/vulkan.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanMaterialBackend;
    class DescriptorSetBuilder
    {
    public:
        static VkWriteDescriptorSet GetUniformWDS( VulkanMaterialBackend* materialBackend, const uint32_t frame,
                                                   const uint32_t set, const uint32_t dstBinding,
                                                   const uint32_t                descriptorCount,
                                                   const VkDescriptorBufferInfo* pBufferInfo );

        static VkWriteDescriptorSet GetStorageWDS( VulkanMaterialBackend* materialBackend, const uint32_t frame,
                                                   const uint32_t set, const uint32_t dstBinding,
                                                   const uint32_t                descriptorCount,
                                                   const VkDescriptorBufferInfo* pBufferInfo );

        static VkWriteDescriptorSet GetSampler2DWDS( VulkanMaterialBackend* materialBackend, const uint32_t frame,
                                                     const uint32_t set, const uint32_t dstBinding,
                                                     const uint32_t               descriptorCount,
                                                     const VkDescriptorImageInfo* pImageInfo );

        static VkWriteDescriptorSet GetSamplerCubeWDS( VulkanMaterialBackend* materialBackend,
                                                       const uint32_t frame, const uint32_t set,
                                                       const uint32_t dstBinding, const uint32_t descriptorCount,
                                                       const VkDescriptorImageInfo* pImageInfo );

        static VkWriteDescriptorSet GetStorageWDS( VulkanMaterialBackend* materialBackend, const uint32_t frame,
                                                   const uint32_t set, const uint32_t dstBinding,
                                                   const uint32_t               descriptorCount,
                                                   const VkDescriptorImageInfo* pImageInfo );
    };
} // namespace Desert::Graphic::API::Vulkan