#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>
#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanHelper.hpp>

namespace Desert::Graphic::API::Vulkan
{
    namespace Utils
    {
        uint32_t CalculateImageSize( uint32_t width, uint32_t height, const Core::Formats::ImageFormat& format )
        {
            uint32_t size = width * height;
            switch ( format )
            {
                case Core::Formats::ImageFormat::RGB:
                    size = size * 3;
                    break;
                case Core::Formats::ImageFormat::RGBA:
                    size = size * 3;
                    break;
            }
            return size;
        }

        void InsertImageMemoryBarrier( VkCommandBuffer CmdBuf, VkImage Image, VkFormat Format,
                                       VkImageLayout OldLayout, VkImageLayout NewLayout )
        {
            VkImageMemoryBarrier barrier = { .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                             .pNext               = NULL,
                                             .srcAccessMask       = 0,
                                             .dstAccessMask       = 0,
                                             .oldLayout           = OldLayout,
                                             .newLayout           = NewLayout,
                                             .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                             .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                             .image               = Image,
                                             .subresourceRange =
                                                  VkImageSubresourceRange{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                                           .baseMipLevel   = 0,
                                                                           .levelCount     = 1,
                                                                           .baseArrayLayer = 0,
                                                                           .layerCount     = 1 } };

            VkPipelineStageFlags sourceStage      = VK_PIPELINE_STAGE_NONE;
            VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_NONE;

            if ( NewLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
                 ( Format == VK_FORMAT_D16_UNORM ) || ( Format == VK_FORMAT_X8_D24_UNORM_PACK32 ) ||
                 ( Format == VK_FORMAT_D32_SFLOAT ) || ( Format == VK_FORMAT_S8_UINT ) ||
                 ( Format == VK_FORMAT_D16_UNORM_S8_UINT ) || ( Format == VK_FORMAT_D24_UNORM_S8_UINT ) )
            {
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

                /*if (HasStencilComponent(Format)) {
                    barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
                }*/
            }
            else
            {
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            }

            if ( OldLayout == VK_IMAGE_LAYOUT_UNDEFINED && NewLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL )
            {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                sourceStage      = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            }
            else if ( OldLayout == VK_IMAGE_LAYOUT_UNDEFINED && NewLayout == VK_IMAGE_LAYOUT_GENERAL )
            {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                sourceStage      = VK_PIPELINE_STAGE_TRANSFER_BIT;
                destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            }

            if ( OldLayout == VK_IMAGE_LAYOUT_UNDEFINED && NewLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL )
            {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

                sourceStage      = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            } /* Convert back from read-only to updateable */
            else if ( OldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
                      NewLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL )
            {
                barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

                sourceStage      = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            } /* Convert from updateable texture to shader read-only */
            else if ( OldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                      NewLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL )
            {
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                sourceStage      = VK_PIPELINE_STAGE_TRANSFER_BIT;
                destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            } /* Convert depth texture from undefined state to depth-stencil buffer */
            else if ( OldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
                      NewLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL )
            {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask =
                     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

                sourceStage      = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            } /* Wait for render pass to complete */
            else if ( OldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
                      NewLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL )
            {
                barrier.srcAccessMask = 0; // VK_ACCESS_SHADER_READ_BIT;
                barrier.dstAccessMask = 0;
                /*
                        sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                ///		destinationStage = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
                        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                */
                sourceStage      = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            } /* Convert back from read-only to color attachment */
            else if ( OldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
                      NewLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL )
            {
                barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

                sourceStage      = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            } /* Convert from updateable texture to shader read-only */
            else if ( OldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
                      NewLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL )
            {
                barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                sourceStage      = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            } /* Convert back from read-only to depth attachment */
            else if ( OldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
                      NewLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL )
            {
                barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

                sourceStage      = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                destinationStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            } /* Convert from updateable depth texture to shader read-only */
            else if ( OldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
                      NewLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL )
            {
                barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                sourceStage      = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            }

            vkCmdPipelineBarrier( CmdBuf, sourceStage, destinationStage, 0, 0, NULL, 0, NULL, 1, &barrier );
        }
    } // namespace Utils

    VulkanImage2D::VulkanImage2D( const Core::Formats::ImageSpecification& specfication )
         : m_ImageSpecification( specfication )
    {
    }

    uint32_t VulkanImage2D::GetMipmapLevels() const
    {
        return 1;
    }

    void VulkanImage2D::Use( uint32_t slot /*= 0 */ ) const
    {
    }

    Common::BoolResult VulkanImage2D::RT_Invalidate()
    {
        VkDevice device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

        DESERT_VERIFY( m_ImageSpecification.Data );

        auto& allocator = VulkanAllocator::GetInstance();

        VkImageCreateInfo imageCreateInfo = {
             .sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
             .pNext     = VK_NULL_HANDLE,
             .imageType = VK_IMAGE_TYPE_2D,
             .format    = VK_FORMAT_R8G8B8A8_UNORM,
             .extent = { .width = m_ImageSpecification.Width, .height = m_ImageSpecification.Height, .depth = 1 },
             .mipLevels   = 1,
             .arrayLayers = 1,
             .samples     = VK_SAMPLE_COUNT_1_BIT,
             .tiling      = VK_IMAGE_TILING_OPTIMAL,
             .usage       = ( VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT ),
             .sharingMode = VK_SHARING_MODE_EXCLUSIVE,

        };

        uint32_t imageSize = Utils::CalculateImageSize( m_ImageSpecification.Width, m_ImageSpecification.Height,
                                                        m_ImageSpecification.Format );

        // Create staging buffer
        VkBufferCreateInfo bufferCreateInfo{};
        bufferCreateInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.size        = imageSize;
        bufferCreateInfo.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkBuffer stagingBuffer;

        const auto stagingBufferAllocation = allocator.RT_AllocateBuffer(
             "Image2D(staging)-TODO: Name", bufferCreateInfo, VMA_MEMORY_USAGE_CPU_TO_GPU, stagingBuffer );
        if ( !stagingBufferAllocation.IsSuccess() )
        {
            return Common::MakeError<bool>( stagingBufferAllocation.GetError() );
        }

        // Copy data to staging buffer
        uint8_t* destData = allocator.MapMemory( stagingBufferAllocation.GetValue() );
        memcpy( destData, m_ImageSpecification.Data.value(), imageSize );
        allocator.UnmapMemory( stagingBufferAllocation.GetValue() );

        const auto buffer = allocator.RT_AllocateImage( "Image2D-TODO: Name", imageCreateInfo,
                                                        VMA_MEMORY_USAGE_GPU_ONLY, m_VulkanImageInfo.Image );
        if ( !buffer.IsSuccess() )
        {
            return Common::MakeError<bool>( buffer.GetError() );
        }

        m_VulkanImageInfo.MemoryAlloc = buffer.GetValue();

        auto copyCmd = CommandBufferAllocator::GetInstance().RT_GetCommandBufferGraphic( true );
        if ( !copyCmd.IsSuccess() )
        {
            return Common::MakeError<bool>( copyCmd.GetError() );
        }

        auto copyCmdVal = copyCmd.GetValue();

        Utils::InsertImageMemoryBarrier( copyCmdVal, m_VulkanImageInfo.Image, VK_FORMAT_R8G8B8A8_UNORM,
                                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );

        VkBufferImageCopy bufferCopyRegion = {
             .bufferOffset      = 0,
             .bufferRowLength   = 0,
             .bufferImageHeight = 0,
             .imageSubresource  = VkImageSubresourceLayers{ .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                                            .mipLevel       = 0,
                                                            .baseArrayLayer = 0,
                                                            .layerCount     = 1 },
             .imageOffset       = VkOffset3D{ .x = 0, .y = 0, .z = 0 },
             .imageExtent       = VkExtent3D{
                        .width = m_ImageSpecification.Width, .height = m_ImageSpecification.Height, .depth = 1 } };

        vkCmdCopyBufferToImage( copyCmdVal, stagingBuffer, m_VulkanImageInfo.Image,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion );

        Utils::InsertImageMemoryBarrier( copyCmdVal, m_VulkanImageInfo.Image, VK_FORMAT_R8G8B8A8_UNORM,
                                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );

        CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( copyCmdVal );

        // TODO   allocator.RT_DestroyBuffer( stagingBuffer, m_VulkanImageInfo.MemoryAlloc );

        // Create a default image view
        VkImageViewCreateInfo imageViewCreateInfo           = {};
        imageViewCreateInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewCreateInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        imageViewCreateInfo.format                          = VK_FORMAT_R8G8B8A8_UNORM;
        imageViewCreateInfo.flags                           = 0;
        imageViewCreateInfo.subresourceRange                = {};
        imageViewCreateInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        imageViewCreateInfo.subresourceRange.baseMipLevel   = 0;
        imageViewCreateInfo.subresourceRange.levelCount     = 1;
        imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
        imageViewCreateInfo.subresourceRange.layerCount     = 1;
        imageViewCreateInfo.image                           = m_VulkanImageInfo.Image;
        VK_CHECK_RESULT(
             vkCreateImageView( device, &imageViewCreateInfo, nullptr, &m_VulkanImageInfo.ImageView ) );

        VkSamplerCreateInfo samplerCreateInfo = {};
        samplerCreateInfo.sType               = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerCreateInfo.maxAnisotropy       = 1.0f;
        samplerCreateInfo.magFilter           = VK_FILTER_LINEAR;
        samplerCreateInfo.minFilter           = VK_FILTER_LINEAR;
        samplerCreateInfo.mipmapMode          = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        samplerCreateInfo.addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerCreateInfo.addressModeV  = samplerCreateInfo.addressModeU;
        samplerCreateInfo.addressModeW  = samplerCreateInfo.addressModeU;
        samplerCreateInfo.mipLodBias    = 0.0f;
        samplerCreateInfo.maxAnisotropy = 1.0f;
        samplerCreateInfo.minLod        = 0.0f;
        samplerCreateInfo.maxLod        = 100.0f;
        samplerCreateInfo.borderColor   = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        VK_CHECK_RESULT( vkCreateSampler( device, &samplerCreateInfo, nullptr, &m_VulkanImageInfo.Sampler ) );
    }

} // namespace Desert::Graphic::API::Vulkan