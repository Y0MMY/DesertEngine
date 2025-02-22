#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>
#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>

namespace Desert::Graphic::API::Vulkan
{
    static constexpr uint32_t LayerCount = 6;

    namespace Utils
    {
        uint32_t CalculateImageSize( uint32_t width, uint32_t height, const Core::Formats::ImageFormat& format )
        {
            uint32_t size = width * height;

            switch ( format )
            {
                case Core::Formats::ImageFormat::RGBA8F:
                    size *= 4; // RGBA8F uses 4 bytes per pixel
                    break;
            }

            return size;
        }

        [[maybe_unused]] std::array<std::vector<unsigned char>, LayerCount>
        GetCubeSidesImageData( const std::byte* originalImageData, uint32_t width, uint32_t height )
        {
            std::array<std::vector<unsigned char>, 6> faces;

            /*
                * +---+---+---+---+
                |   | U |   |   |
                +---+---+---+---+
                | L | F | R | B |
                +---+---+---+---+
                |   | D |   |   |
                +---+---+---+---+
            */
            int faceOffsets[6][2] = {
                 { 0, 1 }, // left
                 { 1, 1 }, // front
                 { 2, 1 }, // right
                 { 3, 1 }, // back
                 { 1, 0 }, // up
                 { 1, 2 }, // down

            };
            unsigned int faceWidth  = width / 4;
            unsigned int faceHeight = height / 3;

            return faces;
        }

        Common::Result<VmaAllocation> CreateStagingBuffer( VulkanAllocator& allocator, uint32_t imageSize,
                                                           VkBuffer& stagingBuffer )
        {
            VkBufferCreateInfo bufferCreateInfo = {};
            bufferCreateInfo.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferCreateInfo.size               = imageSize;
            bufferCreateInfo.usage              = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            bufferCreateInfo.sharingMode        = VK_SHARING_MODE_EXCLUSIVE;

            return allocator.RT_AllocateBuffer( "Image2D(staging)-TODO: Name", bufferCreateInfo,
                                                VMA_MEMORY_USAGE_CPU_TO_GPU, stagingBuffer );
        }

        void CopyDataToStagingBuffer2D( VulkanAllocator& allocator, VkBuffer stagingBuffer,
                                        VmaAllocation stagingBufferAllocation, uint32_t imageSize,
                                        std::byte* data )
        {
            uint8_t* destData = allocator.MapMemory( stagingBufferAllocation );
            memcpy( destData, data, imageSize );
            allocator.UnmapMemory( stagingBufferAllocation );
        }

        void CopyDataToStagingBufferCubemap( VulkanAllocator& allocator, VkBuffer stagingBuffer,
                                             VmaAllocation stagingBufferAllocation, uint32_t imageSize,
                                             std::byte* data )
        {
            uint8_t* destData = allocator.MapMemory( stagingBufferAllocation );
            memcpy( destData, data, imageSize );
            allocator.UnmapMemory( stagingBufferAllocation );
        }

        Common::Result<VmaAllocation> CreateImageBuffer( VulkanAllocator&   allocator,
                                                         VkImageCreateInfo& imageCreateInfo, VkImage& outPut )
        {
            return allocator.RT_AllocateImage( "Image2D-TODO: Name", imageCreateInfo, VMA_MEMORY_USAGE_GPU_ONLY,
                                               outPut );
        }

        Common::Result<VkCommandBuffer> PrepareCommandBuffer()
        {
            return CommandBufferAllocator::GetInstance().RT_AllocateCommandBufferGraphic( true );
        }

        void PerformImageCopy2D( VkCommandBuffer copyCmdVal, VkBuffer stagingBuffer, VkFormat imageVulkanFormat,
                                 const VkImage image, uint32_t width, uint32_t height, uint32_t mipLevels )
        {
            Utils::InsertImageMemoryBarrier( copyCmdVal, image, imageVulkanFormat, VK_IMAGE_LAYOUT_UNDEFINED,
                                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, mipLevels );

            VkBufferImageCopy bufferCopyRegion = {
                 .bufferOffset      = 0,
                 .bufferRowLength   = 0,
                 .bufferImageHeight = 0,
                 .imageSubresource  = VkImageSubresourceLayers{ .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                                                .mipLevel       = 0,
                                                                .baseArrayLayer = 0,
                                                                .layerCount     = 1 },
                 .imageOffset       = VkOffset3D{ .x = 0, .y = 0, .z = 0 },
                 .imageExtent       = VkExtent3D{ .width = width, .height = height, .depth = 1 } };

            vkCmdCopyBufferToImage( copyCmdVal, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                    &bufferCopyRegion );

            // Perform the memory barrier for shader read
            Utils::InsertImageMemoryBarrier( copyCmdVal, image, imageVulkanFormat,
                                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1, mipLevels );

            CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( copyCmdVal );
        }

        void PerformImageCopyCubemap( VkCommandBuffer copyCmdVal, VkImage srcImage, VkImage dstImage,
                                      uint32_t width, uint32_t height, VkFormat format )
        {
            uint32_t faceWidth  = width / 4;
            uint32_t faceHeight = height / 3;

            /*
                *+---+----+----+----+
                |    | +Y |    |    |
                +----+----+----+----+
                | -X | -Z | +X | +Z |
                +----+----+----+----+
                |    | -Y |    |    |
                +----+----+----+----+
            */

            int faceOffsets[6][2] = {
                 { 2, 1 }, // +X
                 { 0, 1 }, // -X
                 { 1, 0 }, // +Y
                 { 1, 2 }, // -Y
                 { 3, 1 }, // +Z
                 { 1, 1 }  // -Z
            };

            VkImageCopy copies[6] = {

                 { // +X
                   .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                   .srcOffset = { static_cast<int32_t>( 2U * faceWidth ), static_cast<int32_t>( faceHeight ), 0 },
                   .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                   .dstOffset      = { 0, 0, 0 },
                   .extent         = { faceWidth, faceHeight, 1 } },
                 { // -X
                   .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                   .srcOffset      = { 0, static_cast<int32_t>( faceHeight ), 0 },
                   .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 1 },
                   .dstOffset      = { 0, 0, 0 },
                   .extent         = { faceWidth, faceHeight, 1 } },
                 { // +Y
                   .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                   .srcOffset      = { static_cast<int32_t>( faceWidth ), 0, 0 },
                   .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 2, 1 },
                   .dstOffset      = { 0, 0, 0 },
                   .extent         = { faceWidth, faceHeight, 1 } },
                 { // -Y
                   .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                   .srcOffset = { static_cast<int32_t>( faceWidth ), static_cast<int32_t>( 2U * faceHeight ), 0 },
                   .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 3, 1 },
                   .dstOffset      = { 0, 0, 0 },
                   .extent         = { faceWidth, faceHeight, 1 } },
                 { // +Z
                   .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                   .srcOffset      = { static_cast<int32_t>( faceWidth ), static_cast<int32_t>( faceHeight ), 0 },
                   .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 4, 1 },
                   .dstOffset      = { 0, 0, 0 },
                   .extent         = { faceWidth, faceHeight, 1 } },
                 { // -Z
                   .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                   .srcOffset = { static_cast<int32_t>( 3U * faceWidth ), static_cast<int32_t>( faceHeight ), 0 },
                   .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 5, 1 },
                   .dstOffset      = { 0, 0, 0 },
                   .extent         = { faceWidth, faceHeight, 1 } } };

            Utils::InsertImageMemoryBarrier( copyCmdVal, srcImage, format,
                                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1, 1 );

            Utils::InsertImageMemoryBarrier( copyCmdVal, dstImage, format, VK_IMAGE_LAYOUT_UNDEFINED,
                                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6, 1 );
            vkCmdCopyImage( copyCmdVal, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImage,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6, copies );

            Utils::InsertImageMemoryBarrier( copyCmdVal, dstImage, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 6, 1 );

            CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( copyCmdVal );
        }

        Common::Result<VkImageView> CreateImageView( VkDevice device, VkFormat imageVulkanFormat,
                                                     const VkImage image, uint32_t mipLevels,
                                                     bool cubemap = false )
        {
            return Utils::CreateImageView( device, image, imageVulkanFormat, VK_IMAGE_ASPECT_COLOR_BIT,
                                           cubemap ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D,
                                           cubemap ? 6 : 1, mipLevels );
        }

        void CreateSampler( VkDevice device, VkSampler& samplerOutput )
        {
            VkSamplerCreateInfo samplerCreateInfo = {};
            samplerCreateInfo.sType               = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerCreateInfo.maxAnisotropy       = 1.0f;
            samplerCreateInfo.magFilter           = VK_FILTER_LINEAR;
            samplerCreateInfo.minFilter           = VK_FILTER_LINEAR;
            samplerCreateInfo.mipmapMode          = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerCreateInfo.addressModeU        = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerCreateInfo.addressModeV        = samplerCreateInfo.addressModeU;
            samplerCreateInfo.addressModeW        = samplerCreateInfo.addressModeU;
            samplerCreateInfo.mipLodBias          = 0.0f;
            samplerCreateInfo.maxAnisotropy       = 1.0f;
            samplerCreateInfo.minLod              = 0.0f;
            samplerCreateInfo.maxLod              = 100.0f;
            samplerCreateInfo.borderColor         = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

            VK_CHECK_RESULT( vkCreateSampler( device, &samplerCreateInfo, nullptr, &samplerOutput ) );
        }

    } // namespace Utils

    VulkanImage2D::VulkanImage2D( const Core::Formats::ImageSpecification& specification )
         : m_ImageSpecification( specification )
    {
        m_MipLevels = // static_cast<uint32_t>(std::floor(
                      // std::log2( std::max( m_ImageSpecification.Width, m_ImageSpecification.Height ) ) ) ) +
             1;
    }

    uint32_t VulkanImage2D::GetMipmapLevels() const
    {
        return m_MipLevels;
    }

    void VulkanImage2D::Use( uint32_t slot /*= 0*/ ) const
    {
    }

    Common::BoolResult VulkanImage2D::RT_Invalidate()
    {
        VkDevice device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

        DESERT_VERIFY( m_ImageSpecification.Data );

        auto&             allocator         = VulkanAllocator::GetInstance();
        VkFormat          imageVulkanFormat = GetImageVulkanFormat( m_ImageSpecification.Format );
        VkImageCreateInfo imageCreateInfo   = GetImageCreateInfo( imageVulkanFormat );

        uint32_t imageSize = Utils::CalculateImageSize( m_ImageSpecification.Width, m_ImageSpecification.Height,
                                                        m_ImageSpecification.Format );

        VkBuffer stagingBuffer;
        auto     stagingBufferAllocation = Utils::CreateStagingBuffer( allocator, imageSize, stagingBuffer );
        if ( !stagingBufferAllocation.IsSuccess() )
        {
            return Common::MakeError<bool>( stagingBufferAllocation.GetError() );
        }

        if ( m_ImageSpecification.Type == Core::Formats::ImageType::ImageCube )
        {

            Utils::CopyDataToStagingBufferCubemap( allocator, stagingBuffer, stagingBufferAllocation.GetValue(),
                                                   imageSize, m_ImageSpecification.Data.value() );
        }
        else
        {
            Utils::CopyDataToStagingBuffer2D( allocator, stagingBuffer, stagingBufferAllocation.GetValue(),
                                              imageSize, m_ImageSpecification.Data.value() );
        }

        auto imageAllocation = Utils::CreateImageBuffer( allocator, imageCreateInfo, m_VulkanImageInfo.Image );
        if ( !imageAllocation.IsSuccess() )
        {
            return Common::MakeError<bool>( imageAllocation.GetError() );
        }

        m_VulkanImageInfo.MemoryAlloc = imageAllocation.GetValue();

        auto copyCmd = Utils::PrepareCommandBuffer();
        if ( !copyCmd.IsSuccess() )
        {
            return Common::MakeError<bool>( copyCmd.GetError() );
        }

        VkCommandBuffer copyCmdVal = copyCmd.GetValue();

        if ( m_ImageSpecification.Type == Core::Formats::ImageType::ImageCube )
        {
            Core::Formats::ImageSpecification specification;
            specification      = m_ImageSpecification;
            specification.Type = Core::Formats::ImageType::Image2D;

            std::shared_ptr<VulkanImage2D> imageSrc = std::make_shared<VulkanImage2D>( specification );
            imageSrc->RT_Invalidate();

            Utils::PerformImageCopyCubemap( copyCmdVal, imageSrc->GetVulkanImageInfo().Image,
                                            m_VulkanImageInfo.Image, m_ImageSpecification.Width,
                                            m_ImageSpecification.Height, imageVulkanFormat );
        }
        else
        {
            Utils::PerformImageCopy2D( copyCmdVal, stagingBuffer, imageVulkanFormat, m_VulkanImageInfo.Image,
                                       m_ImageSpecification.Width, m_ImageSpecification.Height, m_MipLevels );
        }

        // create ImageView
        auto createImageView =
             Utils::CreateImageView( device, imageVulkanFormat, m_VulkanImageInfo.Image, m_MipLevels,
                                     m_ImageSpecification.Type == Core::Formats::ImageType::ImageCube );
        if ( !createImageView.IsSuccess() )
        {
            return Common::MakeError<bool>( createImageView.GetError() );
        }

        m_VulkanImageInfo.ImageView = createImageView.GetValue();

        // create sampler
        Utils::CreateSampler( device, m_VulkanImageInfo.Sampler );

        VKUtils::SetDebugUtilsObjectName( device, VK_OBJECT_TYPE_IMAGE, "Texture", m_VulkanImageInfo.Image );

        return Common::MakeSuccess( true );
    }

    VkImageCreateInfo VulkanImage2D::GetImageCreateInfo( VkFormat imageFormat )
    {
        VkImageCreateFlags flags = {};
        // Cube faces count as array layers in Vulkan
        uint32_t arrayLayers = 1;
        if ( m_ImageSpecification.Type == Core::Formats::ImageType::ImageCube )
        {
            flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
            arrayLayers = 6;
        }

        uint32_t faceWidth  = m_ImageSpecification.Width / 4;
        uint32_t faceHeight = m_ImageSpecification.Height / 3;

        return VkImageCreateInfo{
             .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
             .pNext       = nullptr,
             .flags       = flags,
             .imageType   = VK_IMAGE_TYPE_2D,
             .format      = imageFormat,
             .extent      = { .width  = ( m_ImageSpecification.Type == Core::Formats::ImageType::ImageCube )
                                             ? faceWidth
                                             : m_ImageSpecification.Width,
                              .height = ( m_ImageSpecification.Type == Core::Formats::ImageType::ImageCube )
                                             ? faceHeight
                                             : m_ImageSpecification.Height,
                              .depth  = 1 },
             .mipLevels   = m_MipLevels,
             .arrayLayers = arrayLayers,
             .samples     = VK_SAMPLE_COUNT_1_BIT,
             .tiling      = VK_IMAGE_TILING_OPTIMAL,
             .usage       = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
             .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    }

    VkFormat GetImageVulkanFormat( const Core::Formats::ImageFormat& imageFormat )
    {
        switch ( imageFormat )
        {
            case Core::Formats::ImageFormat::RGBA8F:
                return VK_FORMAT_R8G8B8A8_UNORM;

                // Add more formats if needed
            default:
                return VK_FORMAT_UNDEFINED; // Return an undefined format if not supported
        }
    }

} // namespace Desert::Graphic::API::Vulkan
