#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>
#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>

#include <Common/Utilities/String.hpp>

namespace Desert::Graphic::API::Vulkan
{
    // Constants
    static constexpr uint32_t CUBEMAP_FACE_COUNT = 6;
    static constexpr uint32_t MIN_MIP_LEVELS     = 1;

    namespace Utils
    {
        inline const VkImageLayout GetVkImageLayout( bool isStorage )
        {
            return isStorage ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        inline std::vector<VkImageView> CreateMipImageViews( const uint32_t mipsLevel, const VkImage image,
                                                             const VkDevice device, const VkFormat format,
                                                             const bool isCubeMap, const bool isDepth )
        {
            std::vector<VkImageView> result( mipsLevel );
            for ( uint32_t mip = 0; mip < mipsLevel; ++mip )
            {
                VkImageViewCreateInfo viewInfo       = {};
                viewInfo.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                viewInfo.image                       = image;
                viewInfo.viewType                    = isCubeMap ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format                      = format;
                viewInfo.subresourceRange.aspectMask = isDepth ? (VkImageAspectFlags)VK_IMAGE_ASPECT_DEPTH_BIT
                                                               : (VkImageAspectFlags)VK_IMAGE_ASPECT_COLOR_BIT;
                viewInfo.subresourceRange.baseMipLevel   = mip;
                viewInfo.subresourceRange.levelCount     = 1;
                viewInfo.subresourceRange.baseArrayLayer = 0;
                viewInfo.subresourceRange.layerCount     = isCubeMap ? CUBEMAP_FACE_COUNT : 1;

                if ( vkCreateImageView( device, &viewInfo, nullptr, &result[mip] ) != VK_SUCCESS )
                {
                    for ( uint32_t i = 0; i < mip; ++i )
                    {
                        vkDestroyImageView( device, result[i], nullptr );
                    }
                    LOG_ERROR( "Failed to create image view for mip level " + std::to_string( mip ) );
                }
            }

            return result;
        }

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
        DetermineImageState( const Core::Formats::Image2DSpecification& spec )
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
        Common::Result<VmaAllocation> CreateStagingBuffer( uint32_t bufferSize, VkBuffer& outBuffer )
        {
            VkBufferCreateInfo bufferInfo = { .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                              .size        = bufferSize,
                                              .usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                              .sharingMode = VK_SHARING_MODE_EXCLUSIVE };

            return VulkanAllocator::GetInstance().RT_AllocateBuffer( "ImageStagingBuffer", bufferInfo,
                                                                     VMA_MEMORY_USAGE_CPU_TO_GPU, outBuffer );
        }

        void ReleaseStagingBuffer( VkBuffer inBuffer, const VmaAllocation& allocation )
        {

            return VulkanAllocator::GetInstance().RT_DestroyBuffer( inBuffer, allocation );
        }

        // Copies image data to a staging buffer
        void CopyToStagingBuffer( VkBuffer stagingBuffer, VmaAllocation stagingAllocation, uint32_t dataSize,
                                  const Core::Formats::ImagePixelData& imageData,
                                  Core::Formats::ImageFormat           format )
        {
            if ( !Core::Formats::HasData( imageData ) )
                return;

            void* mappedData = VulkanAllocator::GetInstance().MapMemory( stagingAllocation );

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

            VulkanAllocator::GetInstance().UnmapMemory( stagingAllocation );
        }

        // Creates a Vulkan image with specified parameters
        inline Common::Result<VmaAllocation> CreateImage( const VkImageCreateInfo& imageInfo, VkImage& outImage,
                                                          const std::string& tag )
        {
            return VulkanAllocator::GetInstance().RT_AllocateImage( tag + " - VulkanImage", imageInfo,
                                                                    VMA_MEMORY_USAGE_GPU_ONLY, outImage );
        }

        inline void ReleaseImage( VkImage inImage, const VmaAllocation& allocation )
        {
            return VulkanAllocator::GetInstance().RT_DestroyImage( inImage, allocation );
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
                                                            bool isDepth = false, bool hasStencil = false )
        {
            VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

            if ( isDepth )
            {
                if ( hasStencil )
                {
                    aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
                }
                else
                {
                    aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                }
            }

            return Utils::CreateImageView( device, image, format, aspectMask,
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

        inline Common::BoolResult DestroyImageInfo( const VkDevice device, VulkanImageInfo& info )
        {
            vkDestroyImageView( device, info.ImageInfo.imageView, nullptr );
            vkDestroySampler( device, info.ImageInfo.sampler, nullptr );

            Utils::ReleaseImage( info.Image, info.MemoryAlloc );
            info.MemoryAlloc         = nullptr;
            info.Image               = nullptr;
            info.ImageInfo.imageView = nullptr;
            info.ImageInfo.sampler   = nullptr;

            return BOOLSUCCESS;
        }

        Common::BoolResult CreateStorageImage( VkDevice device, const VkImageCreateInfo& imageInfo,
                                               VulkanImageInfo& imageInfoVulkan, bool isCubeMap,
                                               const std::string& tag )
        {
            auto imageAlloc =
                 Utils::CreateImage( imageInfo, imageInfoVulkan.Image,
                                     tag + " (storage image  " + ( isCubeMap ? "Cube" : "2D" ) + " )" );
            if ( !imageAlloc.IsSuccess() )
            {
                return Common::MakeError<bool>( "Failed to create storage image: " + imageAlloc.GetError() );
            }

            auto cmdBuffer = Utils::GetCommandBuffer();
            if ( !cmdBuffer.IsSuccess() )
            {
                return Common::MakeError<bool>( "Failed to get command buffer" );
            }

            uint32_t layerCount = isCubeMap ? CUBEMAP_FACE_COUNT : 1;

            // Storage images typically use VK_IMAGE_LAYOUT_GENERAL because:
            // - They're both read and written in shaders
            // - Provides best performance for atomic operations
            Utils::InsertImageMemoryBarrier( cmdBuffer.GetValue(), imageInfoVulkan.Image, imageInfo.format,
                                             VK_IMAGE_LAYOUT_UNDEFINED, // Initial layout
                                             VK_IMAGE_LAYOUT_GENERAL,   // Storage-compatible layout
                                             layerCount,                // array layers
                                             imageInfo.mipLevels );

            CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( cmdBuffer.GetValue() );

            VKUtils::SetDebugUtilsObjectName( device, VK_OBJECT_TYPE_IMAGE,
                                              tag + " - StorageImage with layers: " + std::to_string( layerCount ),
                                              imageInfoVulkan.Image );

            imageInfoVulkan.MemoryAlloc = imageAlloc.GetValue();
            return Common::MakeSuccess( true );
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
            case Core::Formats::ImageFormat::DEPTH32F:
                return VK_FORMAT_D32_SFLOAT;
            case Core::Formats::ImageFormat::DEPTH24STENCIL8:
                return VulkanLogicalDevice::GetInstance().GetPhysicalDevice()->GetDepthFormat();
            default:
                return VK_FORMAT_UNDEFINED;
        }
    }

    // VulkanImage2D Implementation
    VulkanImage2D::VulkanImage2D( const Core::Formats::Image2DSpecification& spec ) : m_ImageSpecification( spec )
    {
        m_MipLevels = m_ImageSpecification.Mips;
    }

    void VulkanImage2D::Use( uint32_t slot ) const
    {
        // Implementation would bind the image to a descriptor set
    }

    Common::BoolResult VulkanImage2D::RT_Invalidate()
    {
        if ( m_VulkanImageInfo.MemoryAlloc != nullptr )
        {
            Release();
        }

        VkDevice device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

        VkFormat          format    = GetImageVulkanFormat( m_ImageSpecification.Format );
        VkImageCreateInfo imageInfo = CreateImageInfo( format );

        Common::BoolResult result;

        // Handle different image usage cases
        switch ( m_ImageSpecification.Usage )
        {
            case Core::Formats::Image2DUsage::Attachment:
            {
                if ( m_ImageSpecification.Properties & Core::Formats::ImageProperties::Storage )
                {
                    return Common::MakeError<bool>( "Attachment images don't support Storage property" );
                }
                result = CreateAttachmentImage( device, imageInfo, format );
                break;
            }

            default: // Regular 2D texture
            {
                result = CreateTextureImage( device, imageInfo, format );
                break;
            }
        }

        if ( !result.IsSuccess() )
        {
            return result;
        }

        const bool isDepth    = Graphic::Utils::IsDepthFormat( m_ImageSpecification.Format );
        const bool hasStencil = Graphic::Utils::HasStencilComponent( m_ImageSpecification.Format );

        // Create image view
        auto viewResult = Utils::CreateImageView( device, format, m_VulkanImageInfo.Image, m_MipLevels, false,
                                                  isDepth, hasStencil );
        if ( !viewResult.IsSuccess() )
        {
            return Common::MakeError<bool>( viewResult.GetError() );
        }
        m_VulkanImageInfo.ImageInfo.imageView = viewResult.GetValue();
        m_VulkanImageInfo.ImageInfo.imageLayout =
             Utils::GetVkImageLayout( m_ImageSpecification.Properties & Core::Formats::ImageProperties::Storage );

        // Create sampler (only for non-storage images)
        if ( ( m_ImageSpecification.Properties & Core::Formats::ImageProperties::Sample ) )
        {
            Utils::CreateTextureSampler( device, m_VulkanImageInfo.ImageInfo.sampler );
        }

        m_MipImageViews =
             Utils::CreateMipImageViews( m_MipLevels, m_VulkanImageInfo.Image, device, format, false, isDepth );
        if ( m_MipImageViews.empty() || m_MipImageViews.size() != m_MipLevels )
        {
            Release();
            return Common::MakeError<bool>( "Failed to create mip image views" );
        }

        return Common::MakeSuccess( true );
    }

    // Helper methods for different image types
    Common::BoolResult VulkanImage2D::CreateAttachmentImage( VkDevice device, VkImageCreateInfo& imageInfo,
                                                             VkFormat format )
    {
        auto allocation = Utils::CreateImage( imageInfo, m_VulkanImageInfo.Image,
                                              m_ImageSpecification.Tag + " (attachment Image) " );
        if ( !allocation.IsSuccess() )
        {
            return Common::MakeError<bool>( allocation.GetError() );
        }
        m_VulkanImageInfo.MemoryAlloc = allocation.GetValue();

        return Common::MakeSuccess( true );
    }

    Common::BoolResult VulkanImage2D::CreateTextureImage( VkDevice device, const VkImageCreateInfo& imageInfo,
                                                          VkFormat format )
    {
        if ( m_ImageSpecification.Properties & Core::Formats::ImageProperties::Storage ) [[likely]]
        {
            return Utils::CreateStorageImage( device, imageInfo, m_VulkanImageInfo, false,
                                              m_ImageSpecification.Tag );
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
        auto     stagingAlloc = Utils::CreateStagingBuffer( imageSize, stagingBuffer );
        if ( !stagingAlloc.IsSuccess() )
        {
            return Common::MakeError<bool>( stagingAlloc.GetError() );
        }

        Utils::CopyToStagingBuffer( stagingBuffer, stagingAlloc.GetValue(), imageSize, m_ImageSpecification.Data,
                                    m_ImageSpecification.Format );

        auto imageAlloc = Utils::CreateImage( imageInfo, m_VulkanImageInfo.Image, m_ImageSpecification.Tag );
        if ( !imageAlloc.IsSuccess() )
        {
            return Common::MakeError<bool>( imageAlloc.GetError() );
        }

        auto cmdBuffer = Utils::GetCommandBuffer();
        if ( !cmdBuffer.IsSuccess() )
        {
            return Common::MakeError<bool>( cmdBuffer.GetError() );
        }

        Utils::CopyBufferToImage2D( cmdBuffer.GetValue(), stagingBuffer, format, m_VulkanImageInfo.Image,
                                    m_ImageSpecification.Width, m_ImageSpecification.Height, m_MipLevels );

        VKUtils::SetDebugUtilsObjectName( device, VK_OBJECT_TYPE_IMAGE, m_ImageSpecification.Tag + " - Texture2D",
                                          m_VulkanImageInfo.Image );

        Utils::ReleaseStagingBuffer( stagingBuffer, stagingAlloc.GetValue() );

        m_VulkanImageInfo.MemoryAlloc = imageAlloc.GetValue();
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
            case Core::Formats::Image2DUsage::Attachment:
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

            default: // Regular texture or storage image
            {
                createInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

                // need to think about this better, it's done here to write to mipslevel.
#ifdef DESERT_CONFIG_DEBUG
                if ( m_ImageSpecification.Mips > 1 )
                    createInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
#endif
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

    Common::BoolResult VulkanImage2D::Invalidate()
    {
        return RT_Invalidate();
    }

    Common::BoolResult VulkanImage2D::Release()
    {
        VkDevice device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

        for ( uint32_t i = 0; i < m_MipImageViews.size(); ++i )
        {
            vkDestroyImageView( device, m_MipImageViews[i], nullptr );
            m_MipImageViews[i] = nullptr;
        }

        m_MipImageViews.clear();

        if ( m_VulkanImageInfo.Image )
        {
            Utils::DestroyImageInfo( device, m_VulkanImageInfo );
            m_VulkanImageInfo = VulkanImageInfo{};
        }

        return BOOLSUCCESS;
    }

    VulkanImage2D::~VulkanImage2D()
    {
        Release();
    }

    //***************************************************************************************************//

    VulkanImageCube::VulkanImageCube( const Core::Formats::ImageCubeSpecification& spec )
         : m_ImageSpecification( spec )
    {
        m_MipLevels = m_ImageSpecification.Mips;
    }

    Common::BoolResult VulkanImageCube::RT_Invalidate()
    {
        if ( m_VulkanImageInfo.Image )
        {
            Release();
        }

        const uint32_t faceWidth  = m_ImageSpecification.Width / 4;
        const uint32_t faceHeight = m_ImageSpecification.Height / 3;

        if ( faceWidth != faceHeight )
        {
            return Common::MakeError<bool>( "Cubemap faces must be square" );
        }

        m_FaceSize = faceWidth;

        VkDevice         device    = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();
        VulkanAllocator& allocator = VulkanAllocator::GetInstance();

        VkFormat          format    = GetImageVulkanFormat( m_ImageSpecification.Format );
        VkImageCreateInfo imageInfo = CreateImageInfo( format );

        Common::BoolResult result;
        if ( m_ImageSpecification.Properties & Core::Formats::ImageProperties::Storage )
            result =
                 Utils::CreateStorageImage( device, imageInfo, m_VulkanImageInfo, true, m_ImageSpecification.Tag );
        else
            result = CreateCubemapImage( device, imageInfo, format );

        if ( !result.IsSuccess() )
            return result;

        auto viewResult =
             Utils::CreateImageView( device, format, m_VulkanImageInfo.Image, m_MipLevels, true, false, false );
        if ( !viewResult.IsSuccess() )
        {
            Release();
            return Common::MakeError<bool>( viewResult.GetError() );
        }

        m_VulkanImageInfo.ImageInfo.imageView = viewResult.GetValue();
        m_VulkanImageInfo.ImageInfo.imageLayout =
             Utils::GetVkImageLayout( m_ImageSpecification.Properties & Core::Formats::ImageProperties::Storage );

        if ( ( m_ImageSpecification.Properties & Core::Formats::ImageProperties::Sample ) )
            Utils::CreateTextureSampler( device, m_VulkanImageInfo.ImageInfo.sampler );

        m_MipImageViews =
             Utils::CreateMipImageViews( m_MipLevels, m_VulkanImageInfo.Image, device, format, true, false );
        if ( !m_MipImageViews.size() )
        {
            return Common::MakeError( "TODO" );
        }

        return Common::MakeSuccess( true );
    }

    Common::BoolResult VulkanImageCube::CreateCubemapImage( VkDevice device, const VkImageCreateInfo& imageInfo,
                                                            VkFormat format )
    {
        if ( !Core::Formats::HasData( m_ImageSpecification.Data ) )
        {
            return Common::MakeError<bool>( "No image data provided for cubemap" );
        }

        struct StagingBuffer
        {
            VkBuffer      buffer     = VK_NULL_HANDLE;
            VmaAllocation allocation = nullptr;

            ~StagingBuffer()
            {
                if ( buffer )
                {
                    VulkanAllocator::GetInstance().RT_DestroyBuffer( buffer, allocation );
                }
            }
        } stagingBuffer;

        const uint32_t bytesPerPixel = GetBytesPerPixel( m_ImageSpecification.Format );
        const uint32_t faceSize      = m_FaceSize * m_FaceSize * bytesPerPixel;
        const uint32_t totalSize     = faceSize * CUBEMAP_FACE_COUNT;

        {
            auto stagingAlloc = Utils::CreateStagingBuffer( totalSize, stagingBuffer.buffer );
            if ( !stagingAlloc.IsSuccess() )
            {
                return Common::MakeError<bool>( "Failed to create staging buffer: " + stagingAlloc.GetError() );
            }
            stagingBuffer.allocation = stagingAlloc.GetValue();
        }

        {
            auto srcDataResult =
                 Utils::GetImageDataPointer( m_ImageSpecification.Data, m_ImageSpecification.Format );
            if ( !srcDataResult.IsSuccess() )
            {
                return Common::MakeError<bool>( "Failed to get image data: " + srcDataResult.GetError() );
            }

            uint8_t* dstData =
                 static_cast<uint8_t*>( VulkanAllocator::GetInstance().MapMemory( stagingBuffer.allocation ) );
            CopyImageDataToCubemapFaces( srcDataResult.GetValue(), dstData,
                                         m_ImageSpecification.Width * bytesPerPixel, faceSize, bytesPerPixel );
            VulkanAllocator::GetInstance().UnmapMemory( stagingBuffer.allocation );
        }

        auto imageAlloc =
             Utils::CreateImage( imageInfo, m_VulkanImageInfo.Image, m_ImageSpecification.Tag + " (cube)" );
        if ( !imageAlloc.IsSuccess() )
        {
            return Common::MakeError<bool>( "Failed to create cube image: " + imageAlloc.GetError() );
        }
        m_VulkanImageInfo.MemoryAlloc = imageAlloc.GetValue();

        auto cmdBuffer = Utils::GetCommandBuffer();
        if ( !cmdBuffer.IsSuccess() )
        {
            Release();
            return Common::MakeError<bool>( "Failed to get command buffer: " + cmdBuffer.GetError() );
        }

        CopyStagingToGpuImage( cmdBuffer.GetValue(), stagingBuffer.buffer, format, faceSize );
        CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( cmdBuffer.GetValue() );

        return Common::MakeSuccess( true );
    }

    void VulkanImageCube::CopyImageDataToCubemapFaces( const uint8_t* srcData, uint8_t* dstData,
                                                       uint32_t srcRowPitch, uint32_t faceSize,
                                                       uint32_t bytesPerPixel )
    {
        const struct FaceMapping
        {
            uint32_t srcX, srcY;
            uint32_t faceIndex;
        } faceMapping[6] = {
             { 2, 1, 0 }, // +X
             { 0, 1, 1 }, // -X
             { 1, 0, 2 }, // +Y
             { 1, 2, 3 }, // -Y
             { 1, 1, 4 }, // +Z
             { 3, 1, 5 }  // -Z
        };

        for ( const auto& face : faceMapping )
        {
            const uint32_t faceOffset = face.faceIndex * faceSize;
            const uint32_t srcFaceOffset =
                 ( face.srcY * m_FaceSize * srcRowPitch ) + ( face.srcX * m_FaceSize * bytesPerPixel );

            for ( uint32_t y = 0; y < m_FaceSize; y++ )
            {
                const uint32_t srcOffset = srcFaceOffset + y * srcRowPitch;
                const uint32_t dstOffset = faceOffset + y * m_FaceSize * bytesPerPixel;
                memcpy( dstData + dstOffset, srcData + srcOffset, m_FaceSize * bytesPerPixel );
            }
        }
    }

    void VulkanImageCube::CopyStagingToGpuImage( VkCommandBuffer commandBuffer, VkBuffer stagingBuffer,
                                                 VkFormat format, uint32_t faceSize )
    {
        std::vector<VkBufferImageCopy> copyRegions;
        copyRegions.reserve( CUBEMAP_FACE_COUNT );

        for ( uint32_t face = 0; face < CUBEMAP_FACE_COUNT; face++ )
        {
            copyRegions.push_back( { .bufferOffset      = face * faceSize,
                                     .bufferRowLength   = m_FaceSize,
                                     .bufferImageHeight = m_FaceSize,
                                     .imageSubresource  = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                                            .mipLevel       = 0,
                                                            .baseArrayLayer = face,
                                                            .layerCount     = 1 },
                                     .imageExtent       = { m_FaceSize, m_FaceSize, 1 } } );
        }

        Utils::InsertImageMemoryBarrier( commandBuffer, m_VulkanImageInfo.Image, format, VK_IMAGE_LAYOUT_UNDEFINED,
                                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, CUBEMAP_FACE_COUNT, 1 );

        vkCmdCopyBufferToImage( commandBuffer, stagingBuffer, m_VulkanImageInfo.Image,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>( copyRegions.size() ),
                                copyRegions.data() );

        Utils::InsertImageMemoryBarrier( commandBuffer, m_VulkanImageInfo.Image, format,
                                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, CUBEMAP_FACE_COUNT, 1 );
    }

    void VulkanImageCube::Use( uint32_t slot /*= 0 */ ) const
    {
        // Implementation would bind the image to a descriptor set
    }

    Desert::Core::Formats::ImagePixelData VulkanImageCube::GetImagePixels() const
    {
        return Core::Formats::EmptyPixelData{};
    }

    VkImageCreateInfo VulkanImageCube::CreateImageInfo( VkFormat format )
    {
        VkImageCreateInfo createInfo = {
             .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
             .flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
             .imageType     = VK_IMAGE_TYPE_2D,
             .format        = format,
             .extent        = { m_FaceSize, m_FaceSize, 1 },
             .mipLevels     = m_MipLevels,
             .arrayLayers   = CUBEMAP_FACE_COUNT,
             .samples       = VK_SAMPLE_COUNT_1_BIT,
             .tiling        = VK_IMAGE_TILING_OPTIMAL,
             .usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
             .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
             .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        if ( m_ImageSpecification.Properties & Core::Formats::ImageProperties::Sample )
            createInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

        if ( m_ImageSpecification.Properties & Core::Formats::ImageProperties::Storage )
            createInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;

        return createInfo;
    }

    Common::BoolResult VulkanImageCube::Invalidate()
    {
        return RT_Invalidate();
    }

    Common::BoolResult VulkanImageCube::Release()
    {
        VkDevice device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

        // Destroy all mip image views
        for ( auto& view : m_MipImageViews )
        {
            if ( view )
            {
                vkDestroyImageView( device, view, nullptr );
                view = nullptr;
            }
        }
        m_MipImageViews.clear();

        if ( m_VulkanImageInfo.Image )
        {
            Utils::DestroyImageInfo( device, m_VulkanImageInfo );
            m_VulkanImageInfo = VulkanImageInfo{};
        }

        return BOOLSUCCESS;
    }

    VulkanImageCube::~VulkanImageCube()
    {
        Release();
    }

} // namespace Desert::Graphic::API::Vulkan