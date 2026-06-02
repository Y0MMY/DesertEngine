#pragma once

#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>

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

        uint8_t* MapMemory( VmaAllocation allocation )
        {
            uint8_t* mappedMemory = nullptr;
            vmaMapMemory( VulkanAllocator::GetVMAAllocator(), allocation, (void**)&mappedMemory );
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