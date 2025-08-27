#include <Engine/Graphic/API/Vulkan/VulkanUtils/WriteDescriptorSetBuilder.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanMaterialBackend.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>
#include <Engine/Graphic/Renderer.hpp>

namespace Desert::Graphic::API::Vulkan
{

    VkWriteDescriptorSet DescriptorSetBuilder::GetUniformWDS( VulkanMaterialBackend* materialBackend,
                                                              const uint32_t frame, const uint32_t set,
                                                              const uint32_t                dstBinding,
                                                              const uint32_t                descriptorCount,
                                                              const VkDescriptorBufferInfo* pBufferInfo )
    {
        VkDescriptorSet descriptorSet = materialBackend->GetDescriptorSet( frame, set );
        if ( descriptorSet == VK_NULL_HANDLE )
        {
            VkWriteDescriptorSet writeDescriptor = {};
            writeDescriptor.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeDescriptor.descriptorType       = VK_DESCRIPTOR_TYPE_MAX_ENUM;
            return writeDescriptor;
        }

        VkWriteDescriptorSet writeDescriptor = {};
        writeDescriptor.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptor.descriptorType       = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writeDescriptor.dstSet               = descriptorSet;
        writeDescriptor.dstBinding           = dstBinding;
        writeDescriptor.descriptorCount      = descriptorCount;
        writeDescriptor.pBufferInfo          = pBufferInfo;

        return writeDescriptor;
    }

    namespace Detail
    {
        template <typename TextureType, typename GetFallbackFunc>
        VkWriteDescriptorSet GetSamplerWDSImpl( VulkanMaterialBackend* materialBackend, const uint32_t frame,
                                                const uint32_t set, const uint32_t dstBinding,
                                                const uint32_t               descriptorCount,
                                                const VkDescriptorImageInfo* pImageInfo,
                                                VkDescriptorType descriptorType, GetFallbackFunc getFallback )
        {
            VkDescriptorSet descriptorSet = materialBackend->GetDescriptorSet( frame, set );
            if ( descriptorSet == VK_NULL_HANDLE )
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
            writeDescriptor.dstSet               = descriptorSet;
            writeDescriptor.dstBinding           = dstBinding;
            writeDescriptor.descriptorCount      = descriptorCount;
            writeDescriptor.pImageInfo           = outputImageInfo;
            return writeDescriptor;
        }
    } // namespace Detail

    VkWriteDescriptorSet DescriptorSetBuilder::GetSampler2DWDS( VulkanMaterialBackend* materialBackend,
                                                                const uint32_t frame, const uint32_t set,
                                                                const uint32_t               dstBinding,
                                                                const uint32_t               descriptorCount,
                                                                const VkDescriptorImageInfo* pImageInfo )
    {
        auto getFallback = []()
        { return FallbackTextures::Get().GetFallbackTexture2D( Core::Formats::ImageFormat::RGBA8F ); };

        return Detail::GetSamplerWDSImpl<API::Vulkan::VulkanImage2D>(
             materialBackend, frame, set, dstBinding, descriptorCount, pImageInfo,
             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, getFallback );
    }

    VkWriteDescriptorSet DescriptorSetBuilder::GetSamplerCubeWDS( VulkanMaterialBackend* materialBackend,
                                                                  const uint32_t frame, const uint32_t set,
                                                                  const uint32_t               dstBinding,
                                                                  const uint32_t               descriptorCount,
                                                                  const VkDescriptorImageInfo* pImageInfo )
    {
        auto getFallback = []()
        { return FallbackTextures::Get().GetFallbackTextureCube( Core::Formats::ImageFormat::RGBA8F ); };

        return Detail::GetSamplerWDSImpl<API::Vulkan::VulkanImageCube>(
             materialBackend, frame, set, dstBinding, descriptorCount, pImageInfo,
             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, getFallback );
    }

    VkWriteDescriptorSet DescriptorSetBuilder::GetStorageWDS( VulkanMaterialBackend* materialBackend,
                                                              const uint32_t frame, const uint32_t set,
                                                              const uint32_t               dstBinding,
                                                              const uint32_t               descriptorCount,
                                                              const VkDescriptorImageInfo* pImageInfo )
    {
        VkDescriptorSet descriptorSet = materialBackend->GetDescriptorSet( frame, set );
        if ( descriptorSet == VK_NULL_HANDLE )
        {
            VkWriteDescriptorSet writeDescriptor = {};
            writeDescriptor.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeDescriptor.descriptorType       = VK_DESCRIPTOR_TYPE_MAX_ENUM;
            return writeDescriptor;
        }

        VkWriteDescriptorSet writeDescriptor = {};
        writeDescriptor.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptor.descriptorType       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writeDescriptor.dstSet               = descriptorSet;
        writeDescriptor.dstBinding           = dstBinding;
        writeDescriptor.descriptorCount      = descriptorCount;
        writeDescriptor.pImageInfo           = pImageInfo;
        return writeDescriptor;
    }

} // namespace Desert::Graphic::API::Vulkan