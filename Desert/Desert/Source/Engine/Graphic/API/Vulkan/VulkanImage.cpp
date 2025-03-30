#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>
#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>

namespace Desert::Graphic::API::Vulkan
{
    // Constants
    static constexpr uint32_t CUBEMAP_FACE_COUNT = 6;
    static constexpr uint32_t MIN_MIP_LEVELS     = 1;

    namespace Utils
    {

        uint32_t GetBytesPerPixel( const Core::Formats::ImageFormat& format )
        {

            switch ( format )
            {
                case Core::Formats::ImageFormat::RGBA8F:
                    return 4; // RGBA = 4 channels, 8 bits each
            }

            return 0U;
        }

        // Calculates the byte size of an image based on dimensions and format
        uint32_t CalculateImageSize( uint32_t width, uint32_t height, const Core::Formats::ImageFormat& format )
        {
            uint32_t pixelCount = width * height;
            return pixelCount * GetBytesPerPixel( format );
        }

        // Creates a temporary buffer for transferring image data to GPU
        Common::Result<VmaAllocation> CreateStagingBuffer( VulkanAllocator& allocator, uint32_t bufferSize,
                                                           VkBuffer& outBuffer )
        {
            VkBufferCreateInfo bufferInfo = { .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                              .size        = bufferSize,
                                              .usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                              .sharingMode = VK_SHARING_MODE_EXCLUSIVE };

            return allocator.RT_AllocateBuffer( "ImageStagingBuffer", bufferInfo, VMA_MEMORY_USAGE_CPU_TO_GPU,
                                                outBuffer );
        }

        // Copies image data to a staging buffer
        void CopyToStagingBuffer( VulkanAllocator& allocator, VkBuffer stagingBuffer,
                                  VmaAllocation stagingAllocation, uint32_t dataSize,
                                  const std::optional<std::byte*>& sourceData )
        {
            if ( !sourceData )
                return;

            void* mappedData = allocator.MapMemory( stagingAllocation );
            memcpy( mappedData, *sourceData, dataSize );
            allocator.UnmapMemory( stagingAllocation );
        }

        // Creates a Vulkan image with specified parameters
        Common::Result<VmaAllocation> CreateImage( VulkanAllocator& allocator, VkImageCreateInfo& imageInfo,
                                                   VkImage& outImage )
        {
            return allocator.RT_AllocateImage( "VulkanImage", imageInfo, VMA_MEMORY_USAGE_GPU_ONLY, outImage );
        }

        // Prepares a command buffer for image operations
        Common::Result<VkCommandBuffer> GetCommandBuffer()
        {
            return CommandBufferAllocator::GetInstance().RT_AllocateCommandBufferGraphic( true );
        }

        // Transitions image layout and copies data from buffer to image
        void CopyBufferToImage2D( VkCommandBuffer commandBuffer, VkBuffer sourceBuffer, VkFormat imageFormat,
                                  VkImage destinationImage, uint32_t width, uint32_t height, uint32_t mipLevels )
        {
            // Transition image to transfer destination layout
            Utils::InsertImageMemoryBarrier( commandBuffer, destinationImage, imageFormat,
                                             VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                             1, // Array layers
                                             mipLevels );

            VkBufferImageCopy copyRegion = { .bufferOffset      = 0,
                                             .bufferRowLength   = 0,
                                             .bufferImageHeight = 0,
                                             .imageSubresource  = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                                                    .mipLevel       = 0,
                                                                    .baseArrayLayer = 0,
                                                                    .layerCount     = 1 },
                                             .imageOffset       = { 0, 0, 0 },
                                             .imageExtent       = { width, height, 1 } };

            vkCmdCopyBufferToImage( commandBuffer, sourceBuffer, destinationImage,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );

            // Transition to shader-read layout
            Utils::InsertImageMemoryBarrier( commandBuffer, destinationImage, imageFormat,
                                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                             1, // Array layers
                                             mipLevels );

            CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( commandBuffer );
        }

        // Creates an image view for accessing the image
        Common::Result<VkImageView> CreateImageView( VkDevice device, VkFormat format, VkImage image,
                                                     uint32_t mipLevels, bool isCubemap = false,
                                                     bool isDepth = false )
        {
            return Utils::CreateImageView( device, image, format,
                                           isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
                                           isCubemap ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D,
                                           isCubemap ? CUBEMAP_FACE_COUNT : 1, mipLevels );
        }

        // Creates a sampler for texture filtering
        void CreateTextureSampler( VkDevice device, VkSampler& outSampler )
        {
            VkSamplerCreateInfo samplerInfo = { .sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                                .magFilter     = VK_FILTER_LINEAR,
                                                .minFilter     = VK_FILTER_LINEAR,
                                                .mipmapMode    = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                                                .addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                                .addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                                .addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                                .mipLodBias    = 0.0f,
                                                .maxAnisotropy = 1.0f,
                                                .minLod        = 0.0f,
                                                .maxLod        = 100.0f,
                                                .borderColor   = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE };

            VK_CHECK_RESULT( vkCreateSampler( device, &samplerInfo, nullptr, &outSampler ) );
        }
    } // namespace Utils

    VkFormat GetImageVulkanFormat( const Core::Formats::ImageFormat& format )
    {
        switch ( format )
        {
            case Core::Formats::ImageFormat::RGBA8F:
                return VK_FORMAT_R8G8B8A8_UNORM;
            case Core::Formats::ImageFormat::BGRA8F:
                return VK_FORMAT_B8G8R8A8_UNORM;
            default:
                return VK_FORMAT_UNDEFINED;
        }
    }

    // VulkanImage2D Implementation
    VulkanImage2D::VulkanImage2D( const Core::Formats::ImageSpecification& spec ) : m_ImageSpecification( spec )
    {
        // For now just use 1 mip level
        // Could calculate based on image dimensions:
        // m_MipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
        m_MipLevels = MIN_MIP_LEVELS;
    }

    uint32_t VulkanImage2D::GetMipmapLevels() const
    {
        return m_MipLevels;
    }

    void VulkanImage2D::Use( uint32_t slot ) const
    {
        // Implementation would bind the image to a descriptor set
    }

    Common::BoolResult VulkanImage2D::RT_Invalidate()
    {
        VkDevice         device    = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();
        VulkanAllocator& allocator = VulkanAllocator::GetInstance();

        VkFormat          format    = GetImageVulkanFormat( m_ImageSpecification.Format );
        VkImageCreateInfo imageInfo = CreateImageInfo( format );

        // Handle different image usage cases
        switch ( m_ImageSpecification.Usage )
        {
            case Core::Formats::ImageUsage::Attachment:
                return CreateAttachmentImage( device, allocator, imageInfo, format );

            case Core::Formats::ImageUsage::Storage:
                return CreateStorageImage( device, allocator, imageInfo );

            case Core::Formats::ImageUsage::ImageCube:
                return CreateCubemapImage( device, allocator, imageInfo, format );

            default: // Regular 2D texture
                return CreateTextureImage( device, allocator, imageInfo, format );
        }
    }

    // Helper methods for different image types
    Common::BoolResult VulkanImage2D::CreateAttachmentImage( VkDevice device, VulkanAllocator& allocator,
                                                             VkImageCreateInfo& imageInfo, VkFormat format )
    {
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;

        if ( Graphic::Utils::IsDepthFormat( m_ImageSpecification.Format ) )
            imageInfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        else
            imageInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        auto allocation = Utils::CreateImage( allocator, imageInfo, m_VulkanImageInfo.Image );
        if ( !allocation.IsSuccess() )
        {
            return Common::MakeError<bool>( allocation.GetError() );
        }

        bool isDepth = Graphic::Utils::IsDepthFormat( m_ImageSpecification.Format );
        auto viewResult =
             Utils::CreateImageView( device, format, m_VulkanImageInfo.Image, m_MipLevels, false, isDepth );

        if ( !viewResult.IsSuccess() )
        {
            return Common::MakeError<bool>( viewResult.GetError() );
        }

        m_VulkanImageInfo.ImageView = viewResult.GetValue();
        Utils::CreateTextureSampler( device, m_VulkanImageInfo.Sampler );

        return Common::MakeSuccess( true );
    }

    Common::BoolResult VulkanImage2D::CreateTextureImage( VkDevice device, VulkanAllocator& allocator,
                                                          VkImageCreateInfo& imageInfo, VkFormat format )
    {
        if ( !m_ImageSpecification.Data )
        {
            return Common::MakeError<bool>( "No image data provided" );
        }

        imageInfo.usage =
             VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        uint32_t imageSize = Utils::CalculateImageSize( m_ImageSpecification.Width, m_ImageSpecification.Height,
                                                        m_ImageSpecification.Format );

        VkBuffer stagingBuffer;
        auto     stagingAlloc = Utils::CreateStagingBuffer( allocator, imageSize, stagingBuffer );
        if ( !stagingAlloc.IsSuccess() )
        {
            return Common::MakeError<bool>( stagingAlloc.GetError() );
        }

        Utils::CopyToStagingBuffer( allocator, stagingBuffer, stagingAlloc.GetValue(), imageSize,
                                    m_ImageSpecification.Data );

        auto imageAlloc = Utils::CreateImage( allocator, imageInfo, m_VulkanImageInfo.Image );
        if ( !imageAlloc.IsSuccess() )
        {
            return Common::MakeError<bool>( imageAlloc.GetError() );
        }
        m_VulkanImageInfo.MemoryAlloc = imageAlloc.GetValue();

        auto cmdBuffer = Utils::GetCommandBuffer();
        if ( !cmdBuffer.IsSuccess() )
        {
            return Common::MakeError<bool>( cmdBuffer.GetError() );
        }

        Utils::CopyBufferToImage2D( cmdBuffer.GetValue(), stagingBuffer, format, m_VulkanImageInfo.Image,
                                    m_ImageSpecification.Width, m_ImageSpecification.Height, m_MipLevels );

        auto viewResult = Utils::CreateImageView( device, format, m_VulkanImageInfo.Image, m_MipLevels );

        if ( !viewResult.IsSuccess() )
        {
            return Common::MakeError<bool>( viewResult.GetError() );
        }
        m_VulkanImageInfo.ImageView = viewResult.GetValue();
        Utils::CreateTextureSampler( device, m_VulkanImageInfo.Sampler );

        VKUtils::SetDebugUtilsObjectName( device, VK_OBJECT_TYPE_IMAGE, "Texture2D", m_VulkanImageInfo.Image );

        return Common::MakeSuccess( true );
    }

    Common::BoolResult VulkanImage2D::CreateCubemapImage( VkDevice device, VulkanAllocator& allocator,
                                                          VkImageCreateInfo& imageInfo, VkFormat format )
    {
        if ( !m_ImageSpecification.Data )
        {
            return Common::MakeError<bool>( "No cubemap data provided" );
        }

        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        imageInfo.usage =
             VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        imageInfo.arrayLayers = CUBEMAP_FACE_COUNT;
        imageInfo.extent      = { m_ImageSpecification.Width / 4, m_ImageSpecification.Height / 3, 1 };

        const uint32_t bytesPerPixel = Utils::GetBytesPerPixel( m_ImageSpecification.Format );
        const uint32_t faceWidth     = m_ImageSpecification.Width / 4;
        const uint32_t faceHeight    = m_ImageSpecification.Height / 3;

        // Critical validation
        if ( faceWidth * 4 != m_ImageSpecification.Width || faceHeight * 3 != m_ImageSpecification.Height )
        {
            return Common::MakeError<bool>(
                 "Image dimensions must be exactly divisible by 4 (width) and 3 (height)" );
        }
        if ( faceWidth != faceHeight )
        {
            return Common::MakeError<bool>( "Cubemap faces must be square" );
        }

        const uint32_t faceSize    = faceWidth * faceHeight * bytesPerPixel;
        const uint32_t totalSize   = faceSize * CUBEMAP_FACE_COUNT;
        const uint32_t srcRowPitch = m_ImageSpecification.Width * bytesPerPixel;

        VkBuffer stagingBuffer;
        auto     stagingAlloc = Utils::CreateStagingBuffer( allocator, totalSize, stagingBuffer );
        if ( !stagingAlloc.IsSuccess() )
        {
            return Common::MakeError<bool>( stagingAlloc.GetError() );
        }

        uint8_t*       dstData = static_cast<uint8_t*>( allocator.MapMemory( stagingAlloc.GetValue() ) );
        const uint8_t* srcData = reinterpret_cast<const uint8_t*>( *m_ImageSpecification.Data );

        const struct
        {
            uint32_t srcX, srcY; // Position in source image
            uint32_t faceIndex;  // Vulkan cubemap face index
        } faceMapping[6] = {
             { 2, 1, 0 }, // +X (right)
             { 0, 1, 1 }, // -X (left)
             { 1, 0, 2 }, // +Y (top) - NOTE: Vulkan's Y axis points downward
             { 1, 2, 3 }, // -Y (bottom)
             { 1, 1, 4 }, // +Z (front)
             { 3, 1, 5 }  // -Z (back)
        };

        for ( const auto& face : faceMapping )
        {
            const uint32_t faceOffset = face.faceIndex * faceSize;
            const uint32_t srcFaceOffset =
                 ( face.srcY * faceHeight * srcRowPitch ) + ( face.srcX * faceWidth * bytesPerPixel );

            for ( uint32_t y = 0; y < faceHeight; y++ )
            {
                const uint32_t srcOffset = srcFaceOffset + y * srcRowPitch;
                const uint32_t dstOffset = faceOffset + y * faceWidth * bytesPerPixel;
                memcpy( dstData + dstOffset, srcData + srcOffset, faceWidth * bytesPerPixel );
            }
        }
        allocator.UnmapMemory( stagingAlloc.GetValue() );

        auto imageAlloc = Utils::CreateImage( allocator, imageInfo, m_VulkanImageInfo.Image );
        if ( !imageAlloc.IsSuccess() )
        {
            return Common::MakeError<bool>( imageAlloc.GetError() );
        }
        m_VulkanImageInfo.MemoryAlloc = imageAlloc.GetValue();

        auto cmdBuffer = Utils::GetCommandBuffer();
        if ( !cmdBuffer.IsSuccess() )
        {
            return Common::MakeError<bool>( cmdBuffer.GetError() );
        }

        std::vector<VkBufferImageCopy> copyRegions;
        for ( uint32_t face = 0; face < CUBEMAP_FACE_COUNT; face++ )
        {
            copyRegions.push_back( { .bufferOffset      = face * faceSize,
                                     .bufferRowLength   = faceWidth,  // Important for proper row alignment
                                     .bufferImageHeight = faceHeight, // Important for proper image height
                                     .imageSubresource  = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                                            .mipLevel       = 0,
                                                            .baseArrayLayer = face,
                                                            .layerCount     = 1 },
                                     .imageExtent       = { faceWidth, faceHeight, 1 } } );
        }

        VkCommandBuffer commandBuffer = cmdBuffer.GetValue();

        // Transition to DST_OPTIMAL
        Utils::InsertImageMemoryBarrier( commandBuffer, m_VulkanImageInfo.Image, format, VK_IMAGE_LAYOUT_UNDEFINED,
                                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, CUBEMAP_FACE_COUNT, 1 );

        vkCmdCopyBufferToImage( commandBuffer, stagingBuffer, m_VulkanImageInfo.Image,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>( copyRegions.size() ),
                                copyRegions.data() );

        // Transition to SHADER_READ layout
        Utils::InsertImageMemoryBarrier( commandBuffer, m_VulkanImageInfo.Image, format,
                                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, CUBEMAP_FACE_COUNT, 1 );

        CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( commandBuffer );

        auto viewResult = Utils::CreateImageView( device, format, m_VulkanImageInfo.Image, m_MipLevels, true );
        if ( !viewResult.IsSuccess() )
        {
            return Common::MakeError<bool>( viewResult.GetError() );
        }
        m_VulkanImageInfo.ImageView = viewResult.GetValue();

        VkSamplerCreateInfo samplerInfo = { .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                            .magFilter    = VK_FILTER_LINEAR,
                                            .minFilter    = VK_FILTER_LINEAR,
                                            .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                                            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                            .borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK };
        if ( vkCreateSampler( device, &samplerInfo, nullptr, &m_VulkanImageInfo.Sampler ) != VK_SUCCESS )
        {
            return Common::MakeError<bool>( "Failed to create cubemap sampler" );
        }

        return Common::MakeSuccess( true );
    }

    Common::BoolResult VulkanImage2D::CreateStorageImage( VkDevice device, VulkanAllocator& allocator,
                                                          VkImageCreateInfo& imageInfo )
    {
        // Storage images require VK_IMAGE_USAGE_STORAGE_BIT
        // Adding transfer flags enables CPU->GPU updates if needed
        imageInfo.usage =
             VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        auto imageAlloc = Utils::CreateImage( allocator, imageInfo, m_VulkanImageInfo.Image );
        if ( !imageAlloc.IsSuccess() )
        {
            return Common::MakeError<bool>( "Failed to create storage image: " + imageAlloc.GetError() );
        }
        m_VulkanImageInfo.MemoryAlloc = imageAlloc.GetValue();

        auto cmdBuffer = Utils::GetCommandBuffer();
        if ( !cmdBuffer.IsSuccess() )
        {
            return Common::MakeError<bool>( "Failed to get command buffer" );
        }

        // Storage images typically use VK_IMAGE_LAYOUT_GENERAL because:
        // - They're both read and written in shaders
        // - Provides best performance for atomic operations
        Utils::InsertImageMemoryBarrier( cmdBuffer.GetValue(), m_VulkanImageInfo.Image, imageInfo.format,
                                         VK_IMAGE_LAYOUT_UNDEFINED, // Initial layout
                                         VK_IMAGE_LAYOUT_GENERAL,   // Storage-compatible layout
                                         1,                         // array layers
                                         imageInfo.mipLevels );

        CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( cmdBuffer.GetValue() );

        auto viewResult =
             Utils::CreateImageView( device, imageInfo.format, m_VulkanImageInfo.Image, imageInfo.mipLevels,
                                     false,   // not a cubemap
                                     false ); // not a depth/stencil image
        if ( !viewResult.IsSuccess() )
        {
            return Common::MakeError<bool>( "Failed to create storage image view" );
        }
        m_VulkanImageInfo.ImageView = viewResult.GetValue();

        // Most storage images are accessed via imageLoad/imageStore in shaders
        // rather than through samplers
        m_VulkanImageInfo.Sampler = VK_NULL_HANDLE;

        VKUtils::SetDebugUtilsObjectName( device, VK_OBJECT_TYPE_IMAGE, "StorageImage", m_VulkanImageInfo.Image );

        return Common::MakeSuccess( true );
    }

    VkImageCreateInfo VulkanImage2D::CreateImageInfo( VkFormat format )
    {
        VkImageCreateInfo createInfo = { .sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                         .flags     = 0,
                                         .imageType = VK_IMAGE_TYPE_2D,
                                         .format    = format,
                                         .extent = { m_ImageSpecification.Width, m_ImageSpecification.Height, 1 },
                                         .mipLevels     = m_MipLevels,
                                         .arrayLayers   = 1, // Default, overridden for cubemaps
                                         .samples       = VK_SAMPLE_COUNT_1_BIT,
                                         .tiling        = VK_IMAGE_TILING_OPTIMAL,
                                         .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
                                         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };

        return createInfo;
    }
} // namespace Desert::Graphic::API::Vulkan