#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>
#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>

namespace Desert::Graphic::API::Vulkan
{
    namespace Utils
    {
        uint32_t CalculateImageSize( uint32_t width, uint32_t height, const Core::Formats::ImageFormat& format )
        {
            uint32_t size = width * height;
            switch ( format )
            {
                case Core::Formats::ImageFormat::RGBA8F:
                    size = size * 4;
                    break;
            }
            return size;
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

        VkFormat imageVulkanFormat = GetImageVulkanFormat( m_ImageSpecification.Format );

        VkImageCreateInfo imageCreateInfo = GetImageCreateInfo( imageVulkanFormat );

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

        Utils::InsertImageMemoryBarrier( copyCmdVal, m_VulkanImageInfo.Image, imageVulkanFormat,
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

        Utils::InsertImageMemoryBarrier( copyCmdVal, m_VulkanImageInfo.Image, imageVulkanFormat,
                                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );

        CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( copyCmdVal );

        // TODO   allocator.RT_DestroyBuffer( stagingBuffer, m_VulkanImageInfo.MemoryAlloc );

        // Create a default image view
        const auto createImgaeView =
             Utils::CreateImageView( device, m_VulkanImageInfo.Image, imageVulkanFormat, VK_IMAGE_ASPECT_COLOR_BIT,
                                     VK_IMAGE_VIEW_TYPE_2D, 1, 1 );

        if ( !createImgaeView.IsSuccess() )
        {
            return Common::MakeError<bool>( createImgaeView.GetError() );
        }
        m_VulkanImageInfo.ImageView = createImgaeView.GetValue();

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

        VKUtils::SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_IMAGE, "TODO", m_VulkanImageInfo.Image);
    }

    VkImageCreateInfo VulkanImage2D::GetImageCreateInfo( VkFormat imageFormat )
    {
        return {
             .sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
             .pNext     = VK_NULL_HANDLE,
             .imageType = VK_IMAGE_TYPE_2D,
             .format    = imageFormat,
             .extent = { .width = m_ImageSpecification.Width, .height = m_ImageSpecification.Height, .depth = 1 },
             .mipLevels   = 1,
             .arrayLayers = 1,
             .samples     = VK_SAMPLE_COUNT_1_BIT,
             .tiling      = VK_IMAGE_TILING_OPTIMAL,
             .usage       = ( VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT ),
             .sharingMode = VK_SHARING_MODE_EXCLUSIVE,

        };
    }

    VkFormat GetImageVulkanFormat( const Core::Formats::ImageFormat& imageFormat )
    {
        switch ( imageFormat )
        {
            case Core::Formats::ImageFormat::RGBA8F:
                return VK_FORMAT_R8G8B8A8_UNORM;
        }

        return (VkFormat)0;
    }

} // namespace Desert::Graphic::API::Vulkan