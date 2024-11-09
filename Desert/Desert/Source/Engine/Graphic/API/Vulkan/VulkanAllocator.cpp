#include <Engine/Graphic/API/Vulkan/VulkanAllocator.hpp>

namespace Desert::Graphic::API::Vulkan
{
#define LOG_RENDERER_ALLOCATIONS 1
#if LOG_RENDERER_ALLOCATIONS
#define ALLOCATOR_LOG( ... ) LOG_TRACE( __VA_ARGS__ )
#else
#define ALLOCATOR_LOG( ... )
#endif

    static VmaAllocator s_VmaAllocator;

    namespace
    {
        AllocatedData MakeNewAllocatedInfoData( const std::string& tag, const VmaAllocationInfo& infoAllcation,
                                                const std::vector<AllocatedData>& data )
        {
            AllocatedData newData;
            newData.Tag    = tag;
            newData.Size   = infoAllcation.size;
            newData.Offset = data.size() ? data.back().Offset + data.back().Size : 0;

            return newData;
        }
    } // namespace

    Common::Result<VmaAllocation> VulkanAllocator::RT_AllocateImage( const std::string&       tag,
                                                                     const VkImageCreateInfo& imageCreateInfo,
                                                                     VmaMemoryUsage usage, VkImage& outImage )
    {
        if ( s_VmaAllocator == nullptr )
        {
            return Common::MakeError<VmaAllocation>(
                 "VmaAllocator is nullptr. You should call `VulkanAllocator::Init` first. " );
        }

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.usage                   = usage;

        VmaAllocation allocation;
        vmaCreateImage( s_VmaAllocator, &imageCreateInfo, &allocCreateInfo, &outImage, &allocation, nullptr );

        VmaAllocationInfo allocInfo;
        vmaGetAllocationInfo( s_VmaAllocator, allocation, &allocInfo );

        const auto& infoData = MakeNewAllocatedInfoData( tag, allocInfo, m_AllocatedDataInfo );

        m_AllocatedDataInfo.push_back( infoData );

        ALLOCATOR_LOG( "Allocating image ( {} ); size: {}. ", tag, allocInfo.size );
        ALLOCATOR_LOG( "\tTotal allocated : {}. ", infoData.Offset + infoData.Size );

        return Common::MakeSuccess( allocation );
    }

    void VulkanAllocator::Init( const VulkanLogicalDevice& device, VkInstance instance )
    {
        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.device                 = device.GetVulkanLogicalDevice();
        allocatorInfo.physicalDevice         = device.GetPhysicalDevice()->GetVulkanPhysicalDevice();
        allocatorInfo.vulkanApiVersion       = VK_API_VERSION_1_2;
        allocatorInfo.instance               = instance;

        vmaCreateAllocator( &allocatorInfo, &s_VmaAllocator );
    }

    VmaAllocator& VulkanAllocator::GetVMAAllocator()
    {
        return s_VmaAllocator;
    }

    void VulkanAllocator::UnmapMemory( VmaAllocation allocation )
    {
        vmaUnmapMemory( s_VmaAllocator, allocation );
    }

    Common::Result<VmaAllocation> VulkanAllocator::RT_AllocateBuffer( const std::string&        tag,
                                                                      const VkBufferCreateInfo& bufferCreateInfo,
                                                                      VmaMemoryUsage usage, VkBuffer& outBuffer )
    {
        if ( s_VmaAllocator == nullptr )
        {
            return Common::MakeError<VmaAllocation>(
                 "VmaAllocator is nullptr. You should call `VulkanAllocator::Init` first. " );
        }

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.usage                   = usage;

        VmaAllocation allocation;
        vmaCreateBuffer( s_VmaAllocator, &bufferCreateInfo, &allocCreateInfo, &outBuffer, &allocation, nullptr );

        VmaAllocationInfo allocInfo;
        vmaGetAllocationInfo( s_VmaAllocator, allocation, &allocInfo );

        const auto& infoData = MakeNewAllocatedInfoData( tag, allocInfo, m_AllocatedDataInfo );

        m_AllocatedDataInfo.push_back( infoData );

        ALLOCATOR_LOG( "Allocating image ( {} ); size: {}. ", tag, allocInfo.size );
        ALLOCATOR_LOG( "\tTotal allocated : {}. ", infoData.Offset + infoData.Size );

        return Common::MakeSuccess( allocation );
    }

    void VulkanAllocator::RT_DestroyBuffer( VkBuffer buffer, VmaAllocation allocation )
    {
        vmaDestroyBuffer( s_VmaAllocator, buffer, allocation );
    }

} // namespace Desert::Graphic::API::Vulkan