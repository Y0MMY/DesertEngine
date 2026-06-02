#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>
#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>
#include <Engine/Core/EngineContext.hpp>

#include <Common/Utilities/String.hpp>

namespace Desert::Graphic::API::Vulkan
{
    namespace Utils
    {
        static VkImageLayout GetDefaultLayout( Core::Formats::ImageProperties props )
        {
            if ( props & Core::Formats::Storage ) return VK_IMAGE_LAYOUT_GENERAL;
            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        static void CreateSampler( VkDevice device, VkSampler& outSampler )
        {
            VkSamplerCreateInfo info = {
                 .sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                 .magFilter     = VK_FILTER_LINEAR,
                 .minFilter     = VK_FILTER_LINEAR,
                 .mipmapMode    = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                 .addressModeU  = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                 .addressModeV  = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                 .addressModeW  = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                 .mipLodBias    = 0.0f,
                 .maxAnisotropy = 1.0f,
                 .minLod        = 0.0f,
                 .maxLod        = 100.0f,
                 .borderColor   = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE };

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
        
        VkImageLayout finalDefaultLayout = Utils::GetDefaultLayout( m_Specification.Properties );

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

    void VulkanImage2D::TransitionLayout( VkCommandBuffer cmd, VkImageLayout newLayout, uint32_t mip )
    {
        Graphic::API::Vulkan::Utils::InsertImageMemoryBarrier( cmd, m_Resource.Image, m_Resource.Format, 
                                                               m_Resource.Layout, newLayout, 1, mip == 0 ? m_Resource.MipLevels : 1 );
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
        VkImageLayout finalDefaultLayout = Utils::GetDefaultLayout( m_Specification.Properties );

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

} // namespace Desert::Graphic::API::Vulkan
