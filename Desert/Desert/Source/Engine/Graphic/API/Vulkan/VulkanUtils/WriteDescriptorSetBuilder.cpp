#include <Engine/Graphic/API/Vulkan/VulkanUtils/WriteDescriptorSetBuilder.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>
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

    namespace Detail
    {
        template <typename TextureType, typename GetFallbackFunc>
        VkWriteDescriptorSet GetSamplerWDSImpl( const std::shared_ptr<VulkanShader>& vulkanShader,
                                                const uint32_t frame, const uint32_t set,
                                                const uint32_t dstBinding, const uint32_t descriptorCount,
                                                const VkDescriptorImageInfo* pImageInfo,
                                                VkDescriptorType descriptorType, GetFallbackFunc getFallback )
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

            const VkDescriptorImageInfo* outputImageInfo = pImageInfo;
            if ( pImageInfo->imageView == VK_NULL_HANDLE || pImageInfo->sampler == VK_NULL_HANDLE ||
                 pImageInfo->imageLayout == VK_IMAGE_LAYOUT_UNDEFINED )
            {
                outputImageInfo = &sp_cast<TextureType>( getFallback() )->GetVulkanImageInfo().ImageInfo;
            }

            VkWriteDescriptorSet writeDescriptor = {};
            writeDescriptor.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeDescriptor.descriptorType       = descriptorType;
            writeDescriptor.dstSet               = descriptorSetResult.GetValue().Set;
            writeDescriptor.dstBinding           = dstBinding;
            writeDescriptor.descriptorCount      = descriptorCount;
            writeDescriptor.pImageInfo           = outputImageInfo;
            return writeDescriptor;
        }
    } // namespace Detail

    VkWriteDescriptorSet DescriptorSetBuilder::GetSampler2DWDS( const std::shared_ptr<VulkanShader>& vulkanShader,
                                                                const uint32_t frame, const uint32_t set,
                                                                const uint32_t               dstBinding,
                                                                const uint32_t               descriptorCount,
                                                                const VkDescriptorImageInfo* pImageInfo )
    {
        auto getFallback = []()
        { return FallbackTextures::Get().GetFallbackTexture2D( Core::Formats::ImageFormat::RGBA8F ); };

        return Detail::GetSamplerWDSImpl<API::Vulkan::VulkanImage2D>(
             vulkanShader, frame, set, dstBinding, descriptorCount, pImageInfo,
             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, getFallback );
    }

    VkWriteDescriptorSet DescriptorSetBuilder::GetSamplerCubeWDS(
         const std::shared_ptr<VulkanShader>& vulkanShader, const uint32_t frame, const uint32_t set,
         const uint32_t dstBinding, const uint32_t descriptorCount, const VkDescriptorImageInfo* pImageInfo )
    {
        auto getFallback = []()
        { return FallbackTextures::Get().GetFallbackTextureCube( Core::Formats::ImageFormat::RGBA8F ); };

        return Detail::GetSamplerWDSImpl<API::Vulkan::VulkanImageCube>(
             vulkanShader, frame, set, dstBinding, descriptorCount, pImageInfo,
             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, getFallback );
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