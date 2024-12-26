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
        uint32_t    Offset; // NOTE: currently used to not have the field as TotalAllocatedBytes
    };

    class VulkanAllocator : public Common::Singleton<VulkanAllocator>
    {
    public:
        static auto& GetAllocatedDataInfo()
        {
            return s_AllocatedDataInfo;
        }

        Common::Result<VmaAllocation> RT_AllocateImage( const std::string&       tag,
                                                        const VkImageCreateInfo& imageCreateInfo,
                                                        VmaMemoryUsage usage, VkImage& outImage );

        Common::Result<VmaAllocation> RT_AllocateBuffer( const std::string&        tag,
                                                         const VkBufferCreateInfo& bufferCreateInfo,
                                                         VmaMemoryUsage usage, VkBuffer& outBuffer );

        void RT_DestroyBuffer( VkBuffer buffer, VmaAllocation allocation );

        uint8_t* MapMemory( VmaAllocation allocation )
        {
            uint8_t* mappedMemory = nullptr;
            vmaMapMemory( VulkanAllocator::GetVMAAllocator(), allocation, (void**)&mappedMemory );
            return mappedMemory;
        }

        void UnmapMemory( VmaAllocation allocation );

        void Init( const VulkanLogicalDevice& device, VkInstance instance );

        static VmaAllocator& GetVMAAllocator();

    private:
        VulkanAllocator() = default;

    private:
        static inline std::vector<AllocatedData> s_AllocatedDataInfo;

        friend class Common::Singleton<VulkanAllocator>;
    };
} // namespace Desert::Graphic::API::Vulkan