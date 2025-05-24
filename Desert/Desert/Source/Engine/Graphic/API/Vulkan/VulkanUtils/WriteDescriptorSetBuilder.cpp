#include <Engine/Graphic/API/Vulkan/VulkanUtils/WriteDescriptorSetBuilder.hpp>

namespace Desert::Graphic::API::Vulkan
{

    VkWriteDescriptorSet DescriptorSetBuilder::GetUniformWDS( const std::shared_ptr<VulkanShader>& vulkanShader,
                                                              const uint32_t frame, const uint32_t set,
                                                              const uint32_t                dstBinding,
                                                              const uint32_t                descriptorCount,
                                                              const VkDescriptorBufferInfo* pBufferInfo )
    {
        const auto& dstSet = vulkanShader->GetVulkanDescriptorSetInfo().DescriptorSets[frame][set];

        VkWriteDescriptorSet writeDescriptor = {};
        writeDescriptor.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptor.descriptorType       = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writeDescriptor.dstSet               = dstSet;
        writeDescriptor.dstBinding           = dstBinding;
        writeDescriptor.descriptorCount      = descriptorCount;
        writeDescriptor.pBufferInfo          = pBufferInfo;

        return writeDescriptor;
    }

    VkWriteDescriptorSet DescriptorSetBuilder::GetSamplerWDS( const std::shared_ptr<VulkanShader>& vulkanShader,
                                                              const uint32_t frame, const uint32_t set,
                                                              const uint32_t               dstBinding,
                                                              const uint32_t               descriptorCount,
                                                              const VkDescriptorImageInfo* pImageInfo )
    {
        const auto& dstSet = vulkanShader->GetVulkanDescriptorSetInfo().DescriptorSets[frame][set];

        VkWriteDescriptorSet writeDescriptor = {};
        writeDescriptor.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptor.descriptorType       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptor.dstSet               = dstSet;
        writeDescriptor.dstBinding           = dstBinding;
        writeDescriptor.descriptorCount      = descriptorCount;
        writeDescriptor.pImageInfo           = pImageInfo;
        return writeDescriptor;
    }

    VkWriteDescriptorSet DescriptorSetBuilder::GetStorageWDS( const std::shared_ptr<VulkanShader>& vulkanShader,
                                                              const uint32_t frame, const uint32_t set,
                                                              const uint32_t               dstBinding,
                                                              const uint32_t               descriptorCount,
                                                              const VkDescriptorImageInfo* pImageInfo )
    {
        const auto& dstSet = vulkanShader->GetVulkanDescriptorSetInfo().DescriptorSets[frame][set];

        VkWriteDescriptorSet writeDescriptor = {};
        writeDescriptor.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptor.descriptorType       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writeDescriptor.dstSet               = dstSet;
        writeDescriptor.dstBinding           = dstBinding;
        writeDescriptor.descriptorCount      = descriptorCount;
        writeDescriptor.pImageInfo           = pImageInfo;
        return writeDescriptor;
    }

} // namespace Desert::Graphic::API::Vulkan