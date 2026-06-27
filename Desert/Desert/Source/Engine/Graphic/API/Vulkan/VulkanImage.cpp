#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>
#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>
#include <Engine/Core/EngineContext.hpp>
#include <Engine/Graphic/RenderConfig.hpp>

#include <Common/Utilities/String.hpp>

#include <algorithm>

namespace Desert::Graphic::API::Vulkan
{
    namespace Utils
    {
        static VkImageLayout GetDefaultLayout( Core::Formats::ImageFormat format, Core::Formats::ImageProperties props )
        {
            if ( props & Core::Formats::Storage ) return VK_IMAGE_LAYOUT_GENERAL;
            if ( Graphic::Utils::IsDepthFormat( format ) ) return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        static void CreateSampler( VkDevice device, VkSampler& outSampler )
        {
            // Global filter selected in Scene Settings (pushed into RenderConfig by SceneRenderer):
            // Nearest | Bilinear (linear, nearest mip) | Trilinear (linear, linear mip) | Anisotropic.
            using FM            = Graphic::TextureFilterMode;
            const int  mode     = Graphic::RenderConfig::TextureFilter.load();
            const bool nearest  = mode == static_cast<int>( FM::Nearest );
            const bool linearMip = mode == static_cast<int>( FM::Trilinear ) || mode == static_cast<int>( FM::Anisotropic );

            const VkFilter            filter  = nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
            const VkSamplerMipmapMode mipMode = linearMip ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;

            // Anisotropy: only when requested AND the device supports it (MaxAnisotropy > 1 = supported).
            const float deviceMaxAniso = Graphic::RenderConfig::MaxAnisotropy.load();
            const bool  useAniso       = mode == static_cast<int>( FM::Anisotropic ) && deviceMaxAniso > 1.0f;
            // User-selected level (4/8/16x), clamped to what the device supports.
            const float requestedAniso = static_cast<float>( Graphic::RenderConfig::AnisotropyLevel.load() );
            const float maxAniso =
                 useAniso ? ( requestedAniso < deviceMaxAniso ? requestedAniso : deviceMaxAniso ) : 1.0f;

            VkSamplerCreateInfo info = {
                 .sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                 .magFilter        = filter,
                 .minFilter        = filter,
                 .mipmapMode       = mipMode,
                 .addressModeU     = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                 .addressModeV     = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                 .addressModeW     = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                 .mipLodBias       = 0.0f,
                 .anisotropyEnable = useAniso ? VK_TRUE : VK_FALSE,
                 .maxAnisotropy    = maxAniso,
                 .minLod           = 0.0f,
                 .maxLod           = 100.0f,
                 .borderColor      = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE };

            VK_CHECK_RESULT( vkCreateSampler( device, &info, nullptr, &outSampler ) );
        }

        static VkImageView CreateView( VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspect, 
                                       VkImageViewType type, uint32_t layerCount, uint32_t mipCount, uint32_t baseMip = 0 )
        {
            VkImageViewCreateInfo info = {
                 .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                 .image            = image,
                 .viewType         = type,
                 .format           = format,
                 .subresourceRange = { .aspectMask     = aspect,
                                       .baseMipLevel   = baseMip,
                                       .levelCount     = mipCount,
                                       .baseArrayLayer = 0,
                                       .layerCount     = layerCount } };

            VkImageView view;
            VK_CHECK_RESULT( vkCreateImageView( device, &info, nullptr, &view ) );
            return view;
        }

        static const void* GetPixelDataPtr( const Core::Formats::ImagePixelData& data )
        {
            if ( auto uchar = std::get_if<std::vector<unsigned char>>( &data ) ) return uchar->data();
            if ( auto f32   = std::get_if<std::vector<float>>( &data ) )        return f32->data();
            if ( auto b     = std::get_if<std::byte*>( &data ) )                return *b;
            return nullptr;
        }
    }

    VkFormat GetImageVulkanFormat( const Core::Formats::ImageFormat& format )
    {
        switch ( format )
        {
            case Core::Formats::ImageFormat::RGBA8F:         return VK_FORMAT_R8G8B8A8_UNORM;
            case Core::Formats::ImageFormat::RGBA32F:        return VK_FORMAT_R32G32B32A32_SFLOAT;
            case Core::Formats::ImageFormat::BGRA8F:         return VK_FORMAT_B8G8R8A8_UNORM;
            case Core::Formats::ImageFormat::DEPTH32F:       return VK_FORMAT_D32_SFLOAT;
            case Core::Formats::ImageFormat::DEPTH24STENCIL8: 
                return SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetDevice() )
                     ->GetPhysicalDevice()->GetDepthFormat();
            default: return VK_FORMAT_UNDEFINED;
        }
    }

    // --- VulkanImage2D ---

    VulkanImage2D::VulkanImage2D( const Core::Formats::Image2DSpecification& spec ) : m_Specification( spec ) {}
    VulkanImage2D::~VulkanImage2D() { Release(); }
    void VulkanImage2D::Use( uint32_t slot ) const {}
    Common::BoolResultStr VulkanImage2D::Invalidate() { return RT_Invalidate(); }
    Common::BoolResultStr VulkanImage2D::RT_Invalidate() { Release(); return CreateResource(); }

    Common::BoolResultStr VulkanImage2D::Release()
    {
        if ( !m_Resource.Image ) return BOOLSUCCESS;
        auto allocator = SP_CAST( VulkanContext, EngineContext::GetInstance().GetRendererContext() )->GetVulkanAllocator().get();
        allocator->RT_DestroyImage( m_Resource.Image, m_Resource.Allocation, m_Resource.ImageView, m_Resource.Sampler, m_MipViews );
        m_Resource = {}; m_MipViews.clear(); m_IsLoaded = false;
        return BOOLSUCCESS;
    }

    Common::BoolResultStr VulkanImage2D::CreateResource()
    {
        auto vkDevice = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetDevice() )->GetVulkanLogicalDevice();
        auto allocator = SP_CAST( VulkanContext, EngineContext::GetInstance().GetRendererContext() )->GetVulkanAllocator().get();

        m_Resource.Format     = GetImageVulkanFormat( m_Specification.Format );
        m_Resource.MipLevels  = m_Specification.Mips;
        m_Resource.LayerCount = 1;
        m_Resource.Layout     = VK_IMAGE_LAYOUT_UNDEFINED;

        // Full mip chain requested: floor(log2(max(w,h)))+1 levels (generated from mip 0 below).
        const bool generateMips = m_Specification.GenerateMips && Core::Formats::HasData( m_Specification.Data );
        if ( generateMips )
        {
            uint32_t maxDim = std::max( m_Specification.Width, m_Specification.Height );
            uint32_t levels = 1;
            while ( maxDim > 1 ) { maxDim >>= 1; ++levels; }
            m_Resource.MipLevels = levels;
        }
        
        VkImageLayout finalDefaultLayout = Utils::GetDefaultLayout( m_Specification.Format, m_Specification.Properties );

        VkImageCreateInfo info = {
             .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
             .imageType     = VK_IMAGE_TYPE_2D,
             .format        = m_Resource.Format,
             .extent        = { m_Specification.Width, m_Specification.Height, 1 },
             .mipLevels     = m_Resource.MipLevels,
             .arrayLayers   = 1,
             .samples       = VK_SAMPLE_COUNT_1_BIT,
             .tiling        = VK_IMAGE_TILING_OPTIMAL,
             .usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
             .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
             .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };

        if ( m_Specification.Usage == Core::Formats::Image2DUsage::Attachment )
            info.usage |= Graphic::Utils::IsDepthFormat( m_Specification.Format ) ? 
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        
        if ( m_Specification.Properties & Core::Formats::Storage ) info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;

        auto allocResult = allocator->RT_AllocateImage( m_Specification.Tag, info, VMA_MEMORY_USAGE_GPU_ONLY, m_Resource.Image );
        if ( !allocResult.IsSuccess() ) return Common::MakeError<bool>( allocResult.GetError() );
        m_Resource.Allocation = allocResult.GetValue();

        VkImageAspectFlags aspect = Graphic::Utils::IsDepthFormat( m_Specification.Format ) ? 
            VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        
        m_Resource.ImageView = Utils::CreateView( vkDevice, m_Resource.Image, m_Resource.Format, aspect, VK_IMAGE_VIEW_TYPE_2D, 1, m_Resource.MipLevels );

        if ( m_Specification.Properties & Core::Formats::Sample )
            Utils::CreateSampler( vkDevice, m_Resource.Sampler );

        for ( uint32_t i = 0; i < m_Resource.MipLevels; ++i )
            m_MipViews.push_back( Utils::CreateView( vkDevice, m_Resource.Image, m_Resource.Format, aspect, VK_IMAGE_VIEW_TYPE_2D, 1, 1, i ) );

        auto cmd = CommandBufferAllocator::GetInstance().RT_AllocateCommandBufferGraphic( true ).GetValue();

        if ( Core::Formats::HasData( m_Specification.Data ) )
        {
            uint32_t size = Image::CalculateImageSize( m_Specification.Width, m_Specification.Height, m_Specification.Format );
            VkBuffer staging; VmaAllocation stagingAlloc;
            VkBufferCreateInfo bInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT };
            stagingAlloc = allocator->RT_AllocateBuffer( "Staging", bInfo, VMA_MEMORY_USAGE_CPU_TO_GPU, staging ).GetValue();
            
            void* mapped = allocator->MapMemory( stagingAlloc );
            memcpy( mapped, Utils::GetPixelDataPtr( m_Specification.Data ), size );
            allocator->UnmapMemory( stagingAlloc );

            // Transition UNDEFINED -> TRANSFER_DST_OPTIMAL -> finalDefaultLayout
            TransitionLayout( cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
            
            VkBufferImageCopy copy = { .imageSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1 },
                                       .imageExtent = { m_Specification.Width, m_Specification.Height, 1 } };
            vkCmdCopyBufferToImage( cmd, staging, m_Resource.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy );

            if ( generateMips && m_Resource.MipLevels > 1 )
                GenerateMips( cmd, finalDefaultLayout ); // blit mip 0 down the chain, leaving all in finalLayout
            else
                TransitionLayout( cmd, finalDefaultLayout );

            allocator->RT_DestroyBuffer( staging, stagingAlloc );
        }
        else
        {
            TransitionLayout( cmd, finalDefaultLayout );
        }

        CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( cmd );

        m_IsLoaded = true;
        return Common::MakeSuccess( true );
    }

    namespace
    {
        // Single-mip layout barrier with explicit stage/access masks (the blit chain needs precise
        // per-mip synchronization, which the whole-image TransitionLayout can't express).
        void MipBarrier( VkCommandBuffer cmd, VkImage image, uint32_t mip, VkImageLayout oldLayout,
                         VkImageLayout newLayout, VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                         VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage )
        {
            VkImageMemoryBarrier b = {
                 .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                 .srcAccessMask       = srcAccess,
                 .dstAccessMask       = dstAccess,
                 .oldLayout           = oldLayout,
                 .newLayout           = newLayout,
                 .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                 .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                 .image               = image,
                 .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 1 } };
            vkCmdPipelineBarrier( cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b );
        }
    } // namespace

    void VulkanImage2D::GenerateMips( VkCommandBuffer cmd, VkImageLayout finalLayout )
    {
        // The whole image arrives in TRANSFER_DST_OPTIMAL with mip 0 populated. Walk down the chain:
        // transition the source mip to TRANSFER_SRC, blit (linear) into the next mip, then park the
        // source mip in finalLayout. The last mip is transitioned at the end.
        int32_t mipW = static_cast<int32_t>( m_Specification.Width );
        int32_t mipH = static_cast<int32_t>( m_Specification.Height );

        for ( uint32_t i = 1; i < m_Resource.MipLevels; ++i )
        {
            MipBarrier( cmd, m_Resource.Image, i - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT );

            const int32_t nextW = mipW > 1 ? mipW / 2 : 1;
            const int32_t nextH = mipH > 1 ? mipH / 2 : 1;

            VkImageBlit blit = {
                 .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1 },
                 .srcOffsets     = { { 0, 0, 0 }, { mipW, mipH, 1 } },
                 .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1 },
                 .dstOffsets     = { { 0, 0, 0 }, { nextW, nextH, 1 } } };
            vkCmdBlitImage( cmd, m_Resource.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_Resource.Image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR );

            // Source mip done -> park it in the final sampled layout.
            MipBarrier( cmd, m_Resource.Image, i - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, finalLayout,
                        VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT );

            mipW = nextW;
            mipH = nextH;
        }

        // The last mip never served as a blit source; move it from TRANSFER_DST to the final layout.
        MipBarrier( cmd, m_Resource.Image, m_Resource.MipLevels - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    finalLayout, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT );

        m_Resource.Layout = finalLayout;
    }

    void VulkanImage2D::UploadData( VkCommandBuffer cmd, VkBuffer staging )
    {
        // This is only used for runtime updates, assuming current layout is already SHADER_READ_ONLY_OPTIMAL or similar
        VkImageLayout originalLayout = m_Resource.Layout;
        TransitionLayout( cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
        
        VkBufferImageCopy copy = { .imageSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1 },
                                   .imageExtent = { m_Specification.Width, m_Specification.Height, 1 } };
        vkCmdCopyBufferToImage( cmd, staging, m_Resource.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy );
        
        TransitionLayout( cmd, originalLayout );
    }

    std::vector<uint8_t> VulkanImage2D::ReadPixelsRGBA8()
    {
        const uint32_t w = m_Specification.Width;
        const uint32_t h = m_Specification.Height;
        if ( w == 0 || h == 0 || m_Resource.Image == VK_NULL_HANDLE )
            return {};

        const auto fmt = m_Specification.Format;
        if ( fmt != Core::Formats::ImageFormat::RGBA8F && fmt != Core::Formats::ImageFormat::BGRA8F &&
             fmt != Core::Formats::ImageFormat::RGBA32F )
            return {}; // only color formats we know how to pack

        auto allocator =
             SP_CAST( VulkanContext, EngineContext::GetInstance().GetRendererContext() )->GetVulkanAllocator().get();

        const uint32_t     srcSize = Image::CalculateImageSize( w, h, fmt ); // GPU bytes
        VkBuffer           staging = VK_NULL_HANDLE;
        VkBufferCreateInfo bInfo   = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                       .size  = srcSize,
                                       .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT };
        auto allocRes = allocator->RT_AllocateBuffer( "ThumbReadback", bInfo, VMA_MEMORY_USAGE_GPU_TO_CPU, staging );
        if ( !allocRes.IsSuccess() )
            return {};
        VmaAllocation stagingAlloc = allocRes.GetValue();

        auto                cmd      = CommandBufferAllocator::GetInstance().RT_AllocateCommandBufferGraphic( true ).GetValue();
        const VkImageLayout original = m_Resource.Layout;
        TransitionLayout( cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
        VkBufferImageCopy copy = { .imageSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1 },
                                   .imageExtent      = { w, h, 1 } };
        vkCmdCopyImageToBuffer( cmd, m_Resource.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &copy );
        TransitionLayout( cmd, original == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                                     : original );
        CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( cmd );

        std::vector<uint8_t> raw( srcSize );
        void*                mapped = allocator->MapMemory( stagingAlloc );
        memcpy( raw.data(), mapped, srcSize );
        allocator->UnmapMemory( stagingAlloc );
        allocator->RT_DestroyBuffer( staging, stagingAlloc );

        std::vector<uint8_t> out( static_cast<size_t>( w ) * h * 4 );
        const size_t         pixels = static_cast<size_t>( w ) * h;
        if ( fmt == Core::Formats::ImageFormat::RGBA32F )
        {
            const float* f = reinterpret_cast<const float*>( raw.data() );
            for ( size_t i = 0; i < pixels * 4; ++i )
            {
                float v = f[i];
                v       = v < 0.0f ? 0.0f : ( v > 1.0f ? 1.0f : v );
                out[i]  = static_cast<uint8_t>( v * 255.0f + 0.5f );
            }
        }
        else // RGBA8F / BGRA8F (8-bit; swizzle B<->R for BGRA)
        {
            const bool bgra = ( fmt == Core::Formats::ImageFormat::BGRA8F );
            for ( size_t i = 0; i < pixels; ++i )
            {
                uint8_t r = raw[i * 4 + 0], g = raw[i * 4 + 1], b = raw[i * 4 + 2], a = raw[i * 4 + 3];
                if ( bgra )
                    std::swap( r, b );
                out[i * 4 + 0] = r;
                out[i * 4 + 1] = g;
                out[i * 4 + 2] = b;
                out[i * 4 + 3] = a;
            }
        }
        return out;
    }

    void VulkanImage2D::TransitionLayout( VkCommandBuffer cmd, VkImageLayout newLayout, uint32_t mip )
    {
        Graphic::API::Vulkan::Utils::InsertImageMemoryBarrier( cmd, m_Resource.Image, m_Resource.Format,
                                                               m_Resource.Layout, newLayout, 1, mip == 0 ? m_Resource.MipLevels : 1 );
        m_Resource.Layout = newLayout;
    }

    void VulkanImage2D::TransitionLayout( VkCommandBuffer cmd, VkImageLayout newLayout,
                                          VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                                          VkAccessFlags srcAccess, VkAccessFlags dstAccess )
    {
        VkImageSubresourceRange range{ .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                       .baseMipLevel   = 0,
                                       .levelCount     = m_Resource.MipLevels,
                                       .baseArrayLayer = 0,
                                       .layerCount     = 1 };
        Graphic::API::Vulkan::Utils::InsertImageMemoryBarrier( cmd, m_Resource.Image, srcAccess, dstAccess,
                                                               m_Resource.Layout, newLayout, srcStage,
                                                               dstStage, range );
        m_Resource.Layout = newLayout;
    }

    VkImageView VulkanImage2D::GetMipView( uint32_t level ) const { return m_MipViews[level]; }
    Core::Formats::ImagePixelData VulkanImage2D::GetImagePixels() { DESERT_VERIFY( false ); return {}; }

    // --- VulkanImageCube ---

    VulkanImageCube::VulkanImageCube( const Core::Formats::ImageCubeSpecification& spec ) : m_Specification( spec ) {}
    VulkanImageCube::~VulkanImageCube() { Release(); }
    void VulkanImageCube::Use( uint32_t slot ) const {}
    Common::BoolResultStr VulkanImageCube::Invalidate() { return RT_Invalidate(); }
    Common::BoolResultStr VulkanImageCube::RT_Invalidate() { Release(); return CreateResource(); }

    Common::BoolResultStr VulkanImageCube::Release()
    {
        if ( !m_Resource.Image ) return BOOLSUCCESS;
        auto allocator = SP_CAST( VulkanContext, EngineContext::GetInstance().GetRendererContext() )->GetVulkanAllocator().get();
        allocator->RT_DestroyImage( m_Resource.Image, m_Resource.Allocation, m_Resource.ImageView, m_Resource.Sampler, m_MipViews );
        m_Resource = {}; m_MipViews.clear(); m_IsLoaded = false;
        return BOOLSUCCESS;
    }

    Common::BoolResultStr VulkanImageCube::CreateResource()
    {
        auto vkDevice = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetDevice() )->GetVulkanLogicalDevice();
        auto allocator = SP_CAST( VulkanContext, EngineContext::GetInstance().GetRendererContext() )->GetVulkanAllocator().get();

        m_Resource.Format     = GetImageVulkanFormat( m_Specification.Format );
        m_Resource.MipLevels  = m_Specification.Mips;
        m_Resource.LayerCount = 6;
        m_Resource.Layout     = VK_IMAGE_LAYOUT_UNDEFINED;

        uint32_t faceSize = m_Specification.Width / 4;
        VkImageLayout finalDefaultLayout = Utils::GetDefaultLayout( m_Specification.Format, m_Specification.Properties );

        VkImageCreateInfo info = {
             .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
             .flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
             .imageType     = VK_IMAGE_TYPE_2D,
             .format        = m_Resource.Format,
             .extent        = { faceSize, faceSize, 1 },
             .mipLevels     = m_Resource.MipLevels,
             .arrayLayers   = 6,
             .samples       = VK_SAMPLE_COUNT_1_BIT,
             .tiling        = VK_IMAGE_TILING_OPTIMAL,
             .usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
             .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
             .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };

        if ( m_Specification.Properties & Core::Formats::Storage ) info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;

        auto allocResult = allocator->RT_AllocateImage( m_Specification.Tag, info, VMA_MEMORY_USAGE_GPU_ONLY, m_Resource.Image );
        if ( !allocResult.IsSuccess() ) return Common::MakeError<bool>( allocResult.GetError() );
        m_Resource.Allocation = allocResult.GetValue();

        m_Resource.ImageView = Utils::CreateView( vkDevice, m_Resource.Image, m_Resource.Format, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_CUBE, 6, m_Resource.MipLevels );
        Utils::CreateSampler( vkDevice, m_Resource.Sampler );

        for ( uint32_t i = 0; i < m_Resource.MipLevels; ++i )
            m_MipViews.push_back( Utils::CreateView( vkDevice, m_Resource.Image, m_Resource.Format, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_CUBE, 6, 1, i ) );

        auto cmd = CommandBufferAllocator::GetInstance().RT_AllocateCommandBufferGraphic( true ).GetValue();
        TransitionLayout( cmd, finalDefaultLayout );
        CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( cmd );

        m_IsLoaded = true;
        return Common::MakeSuccess( true );
    }

    void VulkanImageCube::UploadData( VkCommandBuffer cmd, VkBuffer staging ) {}

    void VulkanImageCube::TransitionLayout( VkCommandBuffer cmd, VkImageLayout newLayout, uint32_t mip )
    {
        Graphic::API::Vulkan::Utils::InsertImageMemoryBarrier( cmd, m_Resource.Image, m_Resource.Format, 
                                                               m_Resource.Layout, newLayout, 6, mip == 0 ? m_Resource.MipLevels : 1 );
        m_Resource.Layout = newLayout;
    }

    VkImageView VulkanImageCube::GetMipView( uint32_t level ) const { return m_MipViews[level]; }
    Core::Formats::ImagePixelData VulkanImageCube::GetImagePixels() { return {}; }

    // --- Live sampler recreation (texture-filter setting change) ---

    static void RecreateSamplerImpl( VulkanImageResource& res )
    {
        auto vkDevice =
             SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetDevice() )->GetVulkanLogicalDevice();
        if ( res.Sampler )
            vkDestroySampler( vkDevice, res.Sampler, nullptr );
        res.Sampler = VK_NULL_HANDLE;
        Utils::CreateSampler( vkDevice, res.Sampler ); // reads RenderConfig::TextureFilter
    }

    void VulkanImage2D::RecreateSampler() { RecreateSamplerImpl( m_Resource ); }
    void VulkanImageCube::RecreateSampler() { RecreateSamplerImpl( m_Resource ); }

} // namespace Desert::Graphic::API::Vulkan
