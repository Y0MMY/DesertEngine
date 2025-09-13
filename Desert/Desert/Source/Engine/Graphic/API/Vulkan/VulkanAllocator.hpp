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

    class VulkanAllocator
    {
    public:
        ~VulkanAllocator();

        Common::Result<VmaAllocation> RT_AllocateImage( const std::string&       tag,
                                                        const VkImageCreateInfo& imageCreateInfo,
                                                        VmaMemoryUsage usage, VkImage& outImage );

        Common::Result<VmaAllocation> RT_AllocateBuffer( const std::string&        tag,
                                                         const VkBufferCreateInfo& bufferCreateInfo,
                                                         VmaMemoryUsage usage, VkBuffer& outBuffer );

        void RT_DestroyBuffer( VkBuffer buffer, VmaAllocation allocation );
        void RT_DestroyImage( VkImage image, VmaAllocation allocation );

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
    };
} // namespace Desert::Graphic::API::Vulkan