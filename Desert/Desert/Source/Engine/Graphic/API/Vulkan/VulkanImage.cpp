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

        inline Common::Result<const uint8_t*> GetImageDataPointer( const Core::Formats::ImagePixelData& pixelData,
                                                                   Core::Formats::ImageFormat           format )
        {
            const uint8_t* dataPtr = nullptr;

            std::visit(
                 [&]( auto&& arg )
                 {
                     using T = std::decay_t<decltype( arg )>;

                     if constexpr ( std::is_same_v<T, std::monostate> )
                     {
                         return; // dataPtr = nullptr
                     }
                     else if constexpr ( std::is_same_v<T, std::byte*> )
                     {
                         dataPtr = reinterpret_cast<const uint8_t*>( arg );
                     }
                     else if constexpr ( std::is_same_v<T, std::vector<unsigned char>> )
                     {
                         if ( format == Core::Formats::ImageFormat::RGBA8F ||
                              format == Core::Formats::ImageFormat::BGRA8F )
                         {
                             dataPtr = arg.data();
                         }
                     }
                     else if constexpr ( std::is_same_v<T, std::vector<float>> )
                     {
                         if ( format == Core::Formats::ImageFormat::RGBA32F )
                         {
                             dataPtr = reinterpret_cast<const uint8_t*>( arg.data() );
                         }
                     }
                 },
                 pixelData );

            if ( !dataPtr )
            {
                return Common::MakeError<const uint8_t*>(
                     "Failed to get image data pointer (format mismatch or empty data)" );
            }

            return Common::MakeSuccess( dataPtr );
        }

        Core::Formats::ImagePixelData
        ProcessImageData( const void* mappedData, const Core::Formats::ImageFormat& format, size_t bufferSize )
        {
            switch ( format )
            {
                case Core::Formats::ImageFormat::RGBA8F:
                case Core::Formats::ImageFormat::BGRA8F:
                    // Direct copy for 8-bit formats
                    {
                        Core::Formats::ImagePixelData resultImageData;
                        std::vector<unsigned char>    result( bufferSize );
                        memcpy( result.data(), mappedData, bufferSize );
                        resultImageData = result;

                        return resultImageData;
                        break;
                    }

                case Core::Formats::ImageFormat::RGBA32F:
                {
                    // Convert 32-bit float to 8-bit
                    /* const size_t componentCount = bufferSize / sizeof( float );
                     result.resize( componentCount );

                     const float* srcData = static_cast<const float*>( mappedData );
                     uint8_t*     dstData = result.data();

                     for ( size_t i = 0; i < componentCount; ++i )
                     {
                         dstData[i] = static_cast<uint8_t>( std::clamp( srcData[i], 0.0f, 1.0f ) * 255.0f );
                     }
                     resultImageData = result;*/

                    Core::Formats::ImagePixelData resultImageData;
                    std::vector<float>            result( bufferSize );
                    memcpy( result.data(), mappedData, bufferSize );
                    resultImageData = result;

                    return resultImageData;
                    break;
                }

                default:
                    LOG_ERROR( "Unsupported format for image readback: {}", static_cast<int>( format ) );
                    break;
            }

            return Core::Formats::EmptyPixelData{};
        }

        std::pair<VkImageLayout, VkPipelineStageFlags>
        DetermineImageState( const Core::Formats::ImageSpecification& spec )
        {
            VkPipelineStageFlags sourceStage   = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkImageLayout        currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            // NOTE: now only avalible for compute shader
            if ( spec.Properties & Core::Formats::ImageProperties::Storage )
            {
                currentLayout = VK_IMAGE_LAYOUT_GENERAL;
                sourceStage   = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            }

            return { currentLayout, sourceStage };
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
                                  const Core::Formats::ImagePixelData& imageData,
                                  Core::Formats::ImageFormat           format )
        {
            if ( !Core::Formats::HasData( imageData ) )
                return;

            void* mappedData = allocator.MapMemory( stagingAllocation );

            switch ( format )
            {
                case Core::Formats::ImageFormat::RGBA8F:
                case Core::Formats::ImageFormat::BGRA8F:
                {
                    if ( auto ucharData = Core::Formats::GetUCharData( imageData ) )
                    {
                        memcpy( mappedData, ucharData->data(), dataSize );
                    }
                    break;
                }

                case Core::Formats::ImageFormat::RGBA32F:
                {
                    if ( auto floatData = Core::Formats::GetFloatData( imageData ) )
                    {
                        memcpy( mappedData, floatData->data(), dataSize );
                    }
                    break;
                }

                default:
                    LOG_ERROR( "Unsupported format for staging buffer copy" );
                    break;
            }

            allocator.UnmapMemory( stagingAllocation );
        }

        // Creates a Vulkan image with specified parameters
        inline Common::Result<VmaAllocation> CreateImage( VulkanAllocator&         allocator,
                                                          const VkImageCreateInfo& imageInfo, VkImage& outImage )
        {
            return allocator.RT_AllocateImage( "VulkanImage", imageInfo, VMA_MEMORY_USAGE_GPU_ONLY, outImage );
        }

        // Prepares a command buffer for image operations
        inline Common::Result<VkCommandBuffer> GetCommandBuffer()
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
        inline Common::Result<VkImageView> CreateImageView( VkDevice device, VkFormat format, VkImage image,
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
            case Core::Formats::ImageFormat::RGBA32F:
                return VK_FORMAT_R32G32B32A32_SFLOAT;
            case Core::Formats::ImageFormat::BGRA8F:
                return VK_FORMAT_B8G8R8A8_UNORM;
            default:
                return VK_FORMAT_UNDEFINED;
        }
    }

    // VulkanImage2D Implementation
    VulkanImage2D::VulkanImage2D( const Core::Formats::ImageSpecification& spec ) : m_ImageSpecification( spec )
    {
        if ( m_ImageSpecification.Usage == Core::Formats::ImageUsage::ImageCube ) [[unlikely]]
        {
        }
        else [[likely]]
        {

            m_MipLevels = m_ImageSpecification.Mips;
        }
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

        Common::BoolResult result;

        // Handle different image usage cases
        switch ( m_ImageSpecification.Usage )
        {
            case Core::Formats::ImageUsage::Attachment:
            {
                if ( m_ImageSpecification.Properties & Core::Formats::ImageProperties::Storage )
                {
                    return Common::MakeError<bool>( "Attachment images don't support Storage property" );
                }
                result = CreateAttachmentImage( device, allocator, imageInfo, format );
                break;
            }
            case Core::Formats::ImageUsage::ImageCube:
            {
                result = CreateCubemapImage( device, allocator, imageInfo, format );
                break;
            }

            default: // Regular 2D texture
            {
                result = CreateTextureImage( device, allocator, imageInfo, format );
                break;
            }
        }

        if ( !result.IsSuccess() )
        {
            return result;
        }

        bool isCubemap = m_ImageSpecification.Usage == Core::Formats::ImageUsage::ImageCube;
        bool isDepth   = Graphic::Utils::IsDepthFormat( m_ImageSpecification.Format );

        // Create image view
        auto viewResult =
             Utils::CreateImageView( device, format, m_VulkanImageInfo.Image, m_MipLevels, isCubemap, isDepth );
        if ( !viewResult.IsSuccess() )
        {
            return Common::MakeError<bool>( viewResult.GetError() );
        }
        m_VulkanImageInfo.ImageView = viewResult.GetValue();

        // Create sampler (only for non-storage images)
        if ( ( m_ImageSpecification.Properties & Core::Formats::ImageProperties::Sample ) )
        {
            Utils::CreateTextureSampler( device, m_VulkanImageInfo.Sampler );
        }

        return Common::MakeSuccess( true );
    }

    // Helper methods for different image types
    Common::BoolResult VulkanImage2D::CreateAttachmentImage( VkDevice device, VulkanAllocator& allocator,
                                                             VkImageCreateInfo& imageInfo, VkFormat format )
    {
        if ( Graphic::Utils::IsDepthFormat( m_ImageSpecification.Format ) )
            imageInfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        else
            imageInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        auto allocation = Utils::CreateImage( allocator, imageInfo, m_VulkanImageInfo.Image );
        if ( !allocation.IsSuccess() )
        {
            return Common::MakeError<bool>( allocation.GetError() );
        }

        return Common::MakeSuccess( true );
    }

    Common::BoolResult VulkanImage2D::CreateTextureImage( VkDevice device, VulkanAllocator& allocator,
                                                          const VkImageCreateInfo& imageInfo, VkFormat format )
    {
        if ( m_ImageSpecification.Properties & Core::Formats::ImageProperties::Storage ) [[likely]]
        {
            return CreateStorageImage( device, allocator, imageInfo );
        }
        else [[unlikely]]
        {
            if ( !Core::Formats::HasData( m_ImageSpecification.Data ) )
            {
                return Common::MakeError<bool>( "No image data provided" );
            }
        }

        uint32_t imageSize = Image::CalculateImageSize( m_ImageSpecification.Width, m_ImageSpecification.Height,
                                                        m_ImageSpecification.Format );

        VkBuffer stagingBuffer;
        auto     stagingAlloc = Utils::CreateStagingBuffer( allocator, imageSize, stagingBuffer );
        if ( !stagingAlloc.IsSuccess() )
        {
            return Common::MakeError<bool>( stagingAlloc.GetError() );
        }

        Utils::CopyToStagingBuffer( allocator, stagingBuffer, stagingAlloc.GetValue(), imageSize,
                                    m_ImageSpecification.Data, m_ImageSpecification.Format );

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

        VKUtils::SetDebugUtilsObjectName( device, VK_OBJECT_TYPE_IMAGE, "Texture2D", m_VulkanImageInfo.Image );

        return Common::MakeSuccess( true );
    }

    Common::BoolResult VulkanImage2D::CreateCubemapImage( VkDevice device, VulkanAllocator& allocator,
                                                          const VkImageCreateInfo& imageInfo, VkFormat format )
    {
        if ( m_ImageSpecification.Properties & Core::Formats::ImageProperties::Storage ) [[likely]]
        {
            return CreateStorageImage( device, allocator, imageInfo );
        }
        else [[unlikely]]
        {
            if ( !Core::Formats::HasData( m_ImageSpecification.Data ) )
            {
                return Common::MakeError<bool>( "No image data provided" );
            }
        }

        const uint32_t bytesPerPixel = GetBytesPerPixel( m_ImageSpecification.Format );
        const uint32_t faceWidth     = m_ImageSpecification.Width / 4;
        const uint32_t faceHeight    = m_ImageSpecification.Height / 3;

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

        uint8_t* dstData   = static_cast<uint8_t*>( allocator.MapMemory( stagingAlloc.GetValue() ) );
        auto srcDataResult = Utils::GetImageDataPointer( m_ImageSpecification.Data, m_ImageSpecification.Format );

        if ( !srcDataResult.IsSuccess() )
        {
            return Common::MakeError<bool>( srcDataResult.GetError() );
        }

        const uint8_t* srcData = srcDataResult.GetValue();

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

        return Common::MakeSuccess( true );
    }

    Common::BoolResult VulkanImage2D::CreateStorageImage( VkDevice device, VulkanAllocator& allocator,
                                                          const VkImageCreateInfo& imageInfo )
    {
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

        uint32_t layerCount =
             ( m_ImageSpecification.Usage == Core::Formats::ImageUsage::ImageCube ) ? CUBEMAP_FACE_COUNT : 1;

        // Storage images typically use VK_IMAGE_LAYOUT_GENERAL because:
        // - They're both read and written in shaders
        // - Provides best performance for atomic operations
        Utils::InsertImageMemoryBarrier( cmdBuffer.GetValue(), m_VulkanImageInfo.Image, imageInfo.format,
                                         VK_IMAGE_LAYOUT_UNDEFINED, // Initial layout
                                         VK_IMAGE_LAYOUT_GENERAL,   // Storage-compatible layout
                                         layerCount,                // array layers
                                         imageInfo.mipLevels );

        CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( cmdBuffer.GetValue() );

        VKUtils::SetDebugUtilsObjectName( device, VK_OBJECT_TYPE_IMAGE, "StorageImage", m_VulkanImageInfo.Image );

        return Common::MakeSuccess( true );
    }

    VkImageCreateInfo VulkanImage2D::CreateImageInfo( VkFormat format )
    {
        VkImageCreateInfo createInfo = {
             .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
             .flags         = 0,
             .imageType     = VK_IMAGE_TYPE_2D,
             .format        = format,
             .extent        = { m_ImageSpecification.Width, m_ImageSpecification.Height, 1 },
             .mipLevels     = m_MipLevels,
             .arrayLayers   = 1, // Default, overridden for cubemaps
             .samples       = VK_SAMPLE_COUNT_1_BIT,
             .tiling        = VK_IMAGE_TILING_OPTIMAL,
             .usage         = 0,
             .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
             .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        // Handle different image usage cases
        switch ( m_ImageSpecification.Usage )
        {
            case Core::Formats::ImageUsage::Attachment:
            {
                if ( Graphic::Utils::IsDepthFormat( m_ImageSpecification.Format ) )
                {
                    createInfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                }
                else
                {
                    createInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                }
                break;
            }

            case Core::Formats::ImageUsage::ImageCube:
            {
                createInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
                createInfo.arrayLayers = CUBEMAP_FACE_COUNT;
                createInfo.extent      = { m_ImageSpecification.Width / 4, m_ImageSpecification.Height / 3, 1 };
                createInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                break;
            }
            default: // Regular texture or storage image
            {
                createInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                break;
            }
        }

        if ( m_ImageSpecification.Properties & Core::Formats::ImageProperties::Sample )
        {
            createInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        }
        if ( m_ImageSpecification.Properties & Core::Formats::ImageProperties::Storage )
        {
            createInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
        }

        return createInfo;
    }

    Core::Formats::ImagePixelData VulkanImage2D::GetImagePixels() const
    {
        const uint32_t width         = m_ImageSpecification.Width;
        const uint32_t height        = m_ImageSpecification.Height;
        const VkFormat format        = GetImageVulkanFormat( m_ImageSpecification.Format );
        const uint32_t bytesPerPixel = Image::GetBytesPerPixel( m_ImageSpecification.Format );

        // Create staging buffer
        VkBuffer      stagingBuffer;
        VmaAllocation stagingAllocation;

        const uint32_t     bufferSize = width * height * bytesPerPixel;
        VkBufferCreateInfo bufferInfo = { .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                          .size        = bufferSize,
                                          .usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                          .sharingMode = VK_SHARING_MODE_EXCLUSIVE };

        auto allocationResult = VulkanAllocator::GetInstance().RT_AllocateBuffer(
             "ImageReadbackStaging", bufferInfo, VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer ); // TODO: naming

        if ( !allocationResult.IsSuccess() )
        {
            LOG_ERROR( "Failed to allocate staging buffer" );
            return Core::Formats::EmptyPixelData{};
        }
        stagingAllocation = allocationResult.GetValue();

        auto cmdBufferResult = CommandBufferAllocator::GetInstance().RT_AllocateCommandBufferGraphic( true );
        if ( !cmdBufferResult.IsSuccess() )
        {
            LOG_ERROR( "Failed to allocate command buffer" );
            return Core::Formats::EmptyPixelData{};
        }

        VkCommandBuffer commandBuffer = cmdBufferResult.GetValue();

        // Determine expected state
        const auto [currentLayout, sourceStage] = Utils::DetermineImageState( m_ImageSpecification );

        // Transition to transfer source
        Utils::InsertImageMemoryBarrier( commandBuffer, m_VulkanImageInfo.Image, format, currentLayout,
                                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                         1, // array layers
                                         m_MipLevels );

        VkBufferImageCopy region = {
             .bufferOffset      = 0,
             .bufferRowLength   = 0,
             .bufferImageHeight = 0,
             .imageSubresource  = { .aspectMask /*= Graphic::Utils::IsDepthFormat( m_ImageSpecification.Format )
                                                       ? VK_IMAGE_ASPECT_DEPTH_BIT*/
                                    = VK_IMAGE_ASPECT_COLOR_BIT,
                                    .mipLevel       = 0,
                                    .baseArrayLayer = 0,
                                    .layerCount     = 1 },
             .imageOffset       = { 0, 0, 0 },
             .imageExtent       = { width, height, 1 } };

        vkCmdCopyImageToBuffer( commandBuffer, m_VulkanImageInfo.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                stagingBuffer, 1, &region );

        // Transition back to original layout
        Utils::InsertImageMemoryBarrier( commandBuffer, m_VulkanImageInfo.Image, format,
                                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, currentLayout,
                                         1, // array layers
                                         m_MipLevels );

        CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( commandBuffer );

        void* mappedData;
        vmaMapMemory( VulkanAllocator::GetVMAAllocator(), stagingAllocation, &mappedData );

        Core::Formats::ImagePixelData result =
             Utils::ProcessImageData( mappedData, m_ImageSpecification.Format, bufferSize );

        vmaUnmapMemory( VulkanAllocator::GetVMAAllocator(), stagingAllocation );
        vmaDestroyBuffer( VulkanAllocator::GetVMAAllocator(), stagingBuffer, stagingAllocation );

        return result;
    }

} // namespace Desert::Graphic::API::Vulkan