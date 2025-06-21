#include <Engine/Graphic/API/Vulkan/VulkanUtils/WriteDescriptorSetBuilder.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderer.hpp>
#include <Engine/Graphic/Renderer.hpp>

namespace Desert::Graphic::API::Vulkan
{

    VkWriteDescriptorSet DescriptorSetBuilder::GetUniformWDS( const std::shared_ptr<VulkanShader>& vulkanShader,
                                                              const uint32_t frame, const uint32_t set,
                                                              const uint32_t                dstBinding,
                                                              const uint32_t                descriptorCount,
                                                              const VkDescriptorBufferInfo* pBufferInfo )
    {
        const auto& descriptorSetResult =
             static_cast<API::Vulkan::VulkanRendererAPI*>( Renderer::GetInstance().GetRendererAPI() )
                  ->GetDescriptorManager()
                  ->GetLast( vulkanShader, frame, set );
        if ( !descriptorSetResult.IsSuccess() )
        {
            VkWriteDescriptorSet writeDescriptor = {};
            writeDescriptor.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeDescriptor.descriptorType       = VK_DESCRIPTOR_TYPE_MAX_ENUM;
            return writeDescriptor;
        }

        VkWriteDescriptorSet writeDescriptor = {};
        writeDescriptor.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptor.descriptorType       = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writeDescriptor.dstSet               = descriptorSetResult.GetValue().Set;
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
        const auto& descriptorSetResult =
             static_cast<API::Vulkan::VulkanRendererAPI*>( Renderer::GetInstance().GetRendererAPI() )
                  ->GetDescriptorManager()
                  ->GetLast( vulkanShader, frame, set );
        if ( !descriptorSetResult.IsSuccess() )
        {
            VkWriteDescriptorSet writeDescriptor = {};
            writeDescriptor.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeDescriptor.descriptorType       = VK_DESCRIPTOR_TYPE_MAX_ENUM;
            return writeDescriptor;
        }

        VkWriteDescriptorSet writeDescriptor = {};
        writeDescriptor.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptor.descriptorType       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptor.dstSet               = descriptorSetResult.GetValue().Set;
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
        const auto& descriptorSetResult =
             static_cast<API::Vulkan::VulkanRendererAPI*>( Renderer::GetInstance().GetRendererAPI() )
                  ->GetDescriptorManager()
                  ->GetLast( vulkanShader, frame, set );

        if ( !descriptorSetResult.IsSuccess() )
        {
            VkWriteDescriptorSet writeDescriptor = {};
            writeDescriptor.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeDescriptor.descriptorType       = VK_DESCRIPTOR_TYPE_MAX_ENUM;
            return writeDescriptor;
        }

        VkWriteDescriptorSet writeDescriptor = {};
        writeDescriptor.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptor.descriptorType       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writeDescriptor.dstSet               = descriptorSetResult.GetValue().Set;
        writeDescriptor.dstBinding           = dstBinding;
        writeDescriptor.descriptorCount      = descriptorCount;
        writeDescriptor.pImageInfo           = pImageInfo;
        return writeDescriptor;
    }

} // namespace Desert::Graphic::API::Vulkan