#include <Engine/Graphic/API/Vulkan/VulkanAllocator.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>
#include <Engine/Core/EngineContext.hpp>
#include <Engine/Core/FrameManager.hpp>

namespace Desert::Graphic::API::Vulkan
{
    namespace
    {
        static VmaAllocator s_VmaAllocator = VK_NULL_HANDLE;
    }

    void VulkanAllocator::Init( const std::shared_ptr<VulkanLogicalDevice>& device, VkInstance instance )
    {
        if ( s_VmaAllocator != VK_NULL_HANDLE )
        {
            vmaDestroyAllocator( s_VmaAllocator );
            s_VmaAllocator = VK_NULL_HANDLE;
        }

        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.vulkanApiVersion       = VK_API_VERSION_1_3;
        allocatorInfo.physicalDevice         = device->GetPhysicalDevice()->GetVulkanPhysicalDevice();
        allocatorInfo.device                 = device->GetVulkanLogicalDevice();
        allocatorInfo.instance               = instance;

        VK_CHECK_RESULT( vmaCreateAllocator( &allocatorInfo, &s_VmaAllocator ) );
    }

    void VulkanAllocator::Shutdown()
    {
        if ( s_VmaAllocator != VK_NULL_HANDLE )
        {
            vmaDestroyAllocator( s_VmaAllocator );
            s_VmaAllocator = VK_NULL_HANDLE;
        }
    }

    VmaAllocator& VulkanAllocator::GetVMAAllocator()
    {
        return s_VmaAllocator;
    }

    Common::ResultStr<VmaAllocation> VulkanAllocator::RT_AllocateBuffer( const std::string& tag, const VkBufferCreateInfo& bufferCreateInfo,
                                                                          VmaMemoryUsage usage, VkBuffer& outBuffer )
    {
        if ( s_VmaAllocator == VK_NULL_HANDLE ) return Common::MakeError<VmaAllocation>( "VmaAllocator is null" );

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage                   = usage;

        VkBufferCreateInfo localInfo = bufferCreateInfo;
        if ( localInfo.size == 0 ) localInfo.size = 1; // Vulkan requires size > 0

        VmaAllocation allocation;
        VkResult res = vmaCreateBuffer( s_VmaAllocator, &localInfo, &allocInfo, &outBuffer, &allocation, nullptr );
        if ( res != VK_SUCCESS )
        {
            LOG_ERROR( "[VmaAllocator] vmaCreateBuffer failed for tag '{}' (requested size: {}) with VkResult: {} ({})", tag, bufferCreateInfo.size, (int)res, VkResultToString(res) );
            DESERT_VERIFY( false );
        }

        return Common::MakeSuccess( allocation );
    }

    Common::ResultStr<VmaAllocation> VulkanAllocator::RT_AllocateImage( const std::string& tag, const VkImageCreateInfo& imageCreateInfo,
                                                                         VmaMemoryUsage usage, VkImage& outImage )
    {
        if ( s_VmaAllocator == VK_NULL_HANDLE ) return Common::MakeError<VmaAllocation>( "VmaAllocator is null" );

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage                   = usage;

        VmaAllocation allocation;
        VkResult res = vmaCreateImage( s_VmaAllocator, &imageCreateInfo, &allocInfo, &outImage, &allocation, nullptr );
        if ( res != VK_SUCCESS )
        {
            LOG_ERROR( "[VmaAllocator] vmaCreateImage failed for tag '{}' with VkResult: {} ({})", tag, (int)res, VkResultToString(res) );
            DESERT_VERIFY( false );
        }

        return Common::MakeSuccess( allocation );
    }

    void VulkanAllocator::RT_DestroyBuffer( VkBuffer buffer, VmaAllocation allocation )
    {
        if ( !buffer || !allocation ) return;
        uint32_t frameIndex = Engine::FrameManager::GetInstance().GetCurrentFrameIndex();
        m_BufferDeletionQueue.push_back( { buffer, allocation, frameIndex } );
    }

    void VulkanAllocator::RT_DestroyImage( VkImage image, VmaAllocation allocation, VkImageView imageView,
                                           VkSampler sampler, const std::vector<VkImageView>& mipImageViews )
    {
        if ( !image || !allocation ) return;
        uint32_t frameIndex = Engine::FrameManager::GetInstance().GetCurrentFrameIndex();
        m_ImageDeletionQueue.push_back( { image, allocation, imageView, sampler, mipImageViews, frameIndex } );
    }

    void VulkanAllocator::RT_DestroyFramebuffer( VkFramebuffer framebuffer )
    {
        if ( !framebuffer ) return;
        uint32_t frameIndex = Engine::FrameManager::GetInstance().GetCurrentFrameIndex();
        m_FramebufferDeletionQueue.push_back( { framebuffer, frameIndex } );
    }

    void VulkanAllocator::RT_DestroyRenderPass( VkRenderPass renderPass )
    {
        if ( !renderPass ) return;
        uint32_t frameIndex = Engine::FrameManager::GetInstance().GetCurrentFrameIndex();
        m_RenderPassDeletionQueue.push_back( { renderPass, frameIndex } );
    }

    void VulkanAllocator::UnmapMemory( VmaAllocation allocation )
    {
        if ( s_VmaAllocator != VK_NULL_HANDLE ) vmaUnmapMemory( s_VmaAllocator, allocation );
    }

    void VulkanAllocator::ProcessDeletionQueue()
    {
        if ( s_VmaAllocator == VK_NULL_HANDLE ) return;

        uint32_t frameIndex = Engine::FrameManager::GetInstance().GetCurrentFrameIndex();
        auto device = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetDevice() )->GetVulkanLogicalDevice();

        for ( auto it = m_BufferDeletionQueue.begin(); it != m_BufferDeletionQueue.end(); )
        {
            if ( it->FrameIndex == frameIndex )
            {
                vmaDestroyBuffer( s_VmaAllocator, it->Buffer, it->Allocation );
                it = m_BufferDeletionQueue.erase( it );
            }
            else ++it;
        }

        for ( auto it = m_ImageDeletionQueue.begin(); it != m_ImageDeletionQueue.end(); )
        {
            if ( it->FrameIndex == frameIndex )
            {
                if ( it->ImageView != VK_NULL_HANDLE ) vkDestroyImageView( device, it->ImageView, nullptr );
                if ( it->Sampler != VK_NULL_HANDLE )   vkDestroySampler( device, it->Sampler, nullptr );
                for ( auto view : it->MipImageViews )  vkDestroyImageView( device, view, nullptr );

                vmaDestroyImage( s_VmaAllocator, it->Image, it->Allocation );
                it = m_ImageDeletionQueue.erase( it );
            }
            else ++it;
        }

        for ( auto it = m_FramebufferDeletionQueue.begin(); it != m_FramebufferDeletionQueue.end(); )
        {
            if ( it->FrameIndex == frameIndex )
            {
                vkDestroyFramebuffer( device, it->Framebuffer, nullptr );
                it = m_FramebufferDeletionQueue.erase( it );
            }
            else ++it;
        }

        for ( auto it = m_RenderPassDeletionQueue.begin(); it != m_RenderPassDeletionQueue.end(); )
        {
            if ( it->FrameIndex == frameIndex )
            {
                vkDestroyRenderPass( device, it->RenderPass, nullptr );
                it = m_RenderPassDeletionQueue.erase( it );
            }
            else ++it;
        }
    }

#ifdef DESERT_CONFIG_DEBUG
    void VulkanAllocator::CheckResourceLeaks() {}
#endif

    VulkanAllocator::~VulkanAllocator() {}

} // namespace Desert::Graphic::API::Vulkan
