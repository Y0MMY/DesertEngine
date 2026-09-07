#pragma once

#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>

#include <VulkanAllocator/vk_mem_alloc.h>

namespace Desert::Graphic::API::Vulkan
{
    struct AllocatedData
    {
        std::string Tag;
        uint32_t    Size;
    };

    struct BufferDeletionEntry
    {
        VkBuffer      Buffer;
        VmaAllocation Allocation;
        uint32_t      FrameIndex;
    };

    struct ImageDeletionEntry
    {
        VkImage                  Image;
        VmaAllocation            Allocation;
        VkImageView              ImageView;
        VkSampler                Sampler;
        std::vector<VkImageView> MipImageViews;
        uint32_t                 FrameIndex;
    };

    struct FramebufferDeletionEntry
    {
        VkFramebuffer Framebuffer;
        uint32_t      FrameIndex;
    };

    struct RenderPassDeletionEntry
    {
        VkRenderPass RenderPass;
        uint32_t     FrameIndex;
    };

    class VulkanAllocator
    {
    public:
        ~VulkanAllocator();

        Common::ResultStr<VmaAllocation> RT_AllocateImage( const std::string&       tag,
                                                        const VkImageCreateInfo& imageCreateInfo,
                                                        VmaMemoryUsage usage, VkImage& outImage );

        Common::ResultStr<VmaAllocation> RT_AllocateBuffer( const std::string&        tag,
                                                         const VkBufferCreateInfo& bufferCreateInfo,
                                                         VmaMemoryUsage usage, VkBuffer& outBuffer );

        void RT_DestroyBuffer( VkBuffer buffer, VmaAllocation allocation );
        void RT_DestroyImage( VkImage image, VmaAllocation allocation, VkImageView imageView = VK_NULL_HANDLE,
                              VkSampler sampler = VK_NULL_HANDLE,
                              const std::vector<VkImageView>& mipImageViews = {} );
        void RT_DestroyFramebuffer( VkFramebuffer framebuffer );
        void RT_DestroyRenderPass( VkRenderPass renderPass );

        void ProcessDeletionQueue();

        /// nullptr when the mapping failed, and it SAYS WHY when that happens. The result used to go
        /// straight on the floor, so a failed map was indistinguishable from a successful one that
        /// happened to hand back nothing — and most callers write into the pointer without asking.
        ///
        /// (Those callers are a separate defect and are named as one: eleven of them memcpy into this
        /// return value unchecked, which is a null dereference rather than a refusal. This function can
        /// only make the failure legible; fixing the call sites is not this change's business.)
        uint8_t* MapMemory( VmaAllocation allocation )
        {
            uint8_t*       mappedMemory = nullptr;
            const VkResult mapped =
                 vmaMapMemory( VulkanAllocator::GetVMAAllocator(), allocation, (void**)&mappedMemory );
            if ( mapped != VK_SUCCESS )
            {
                (void)NoteIfDeviceLost( mapped, "vmaMapMemory", __FILE__, __LINE__ );
                LOG_ERROR( "[Allocator] vmaMapMemory failed: {}; the caller gets nullptr.",
                           VkResultToString( mapped ) );
                return nullptr;
            }
            return mappedMemory;
        }

        void UnmapMemory( VmaAllocation allocation );

        void Init( const std::shared_ptr<VulkanLogicalDevice>& device, VkInstance instance );

        void Shutdown();

#ifdef DESERT_CONFIG_DEBUG
        void CheckResourceLeaks();
#endif

        static VmaAllocator& GetVMAAllocator();

        VulkanAllocator() = default;

    private:
        friend class Common::Singleton<VulkanAllocator>;

        std::vector<BufferDeletionEntry>      m_BufferDeletionQueue;
        std::vector<ImageDeletionEntry>       m_ImageDeletionQueue;
        std::vector<FramebufferDeletionEntry> m_FramebufferDeletionQueue;
        std::vector<RenderPassDeletionEntry>  m_RenderPassDeletionQueue;
    };
} // namespace Desert::Graphic::API::Vulkan