#include <Engine/Graphic/API/Vulkan/VulkanAllocator.hpp>
#include <unordered_map>

namespace Desert::Graphic::API::Vulkan
{
#define LOG_RENDERER_ALLOCATIONS 1
#if LOG_RENDERER_ALLOCATIONS
#define ALLOCATOR_LOG( ... ) LOG_TRACE( __VA_ARGS__ )
#else
#define ALLOCATOR_LOG( ... )
#endif

    static VmaAllocator                                s_VmaAllocator        = nullptr;
    static uint32_t                                    s_TotalAllocatedBytes = 0U;
    static std::unordered_map<VkBuffer, AllocatedData> s_BufferAllocationSizes;
    static std::unordered_map<VkImage, AllocatedData>  s_ImageAllocationSizes;

#ifdef DESERT_CONFIG_DEBUG
#define CHECK_RESOURCE_LEAKS()                                                                                    \
    DESERT_VERIFY( s_BufferAllocationSizes.empty() && s_ImageAllocationSizes.empty(),                             \
                   "Not all Vulkan resources were released!" );
#else
#define CHECK_RESOURCE_LEAKS()
#endif

    namespace
    {
        AllocatedData MakeNewAllocatedInfoData( const std::string& tag, const VmaAllocationInfo& infoAllcation )
        {
            AllocatedData newData;
            newData.Tag  = tag;
            newData.Size = infoAllcation.size;

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
                 "VmaAllocator is nullptr. You must call `VulkanAllocator::Init` first. " );
        }

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.usage                   = usage;

        VmaAllocation allocation;
        vmaCreateImage( s_VmaAllocator, &imageCreateInfo, &allocCreateInfo, &outImage, &allocation, nullptr );

        VmaAllocationInfo allocInfo;
        vmaGetAllocationInfo( s_VmaAllocator, allocation, &allocInfo );

        const auto& infoData = MakeNewAllocatedInfoData( tag, allocInfo );

        s_ImageAllocationSizes[outImage] = infoData;
        s_TotalAllocatedBytes += allocInfo.size;

        ALLOCATOR_LOG( "Allocating image ( {} ); size: {}. ", tag, allocInfo.size );
        ALLOCATOR_LOG( "\tTotal allocated : {}. ", s_TotalAllocatedBytes );

        return Common::MakeSuccess( allocation );
    }

    void VulkanAllocator::Init( const std::shared_ptr<VulkanLogicalDevice>& device, VkInstance instance )
    {
        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.device                 = device->GetVulkanLogicalDevice();
        allocatorInfo.physicalDevice         = device->GetPhysicalDevice()->GetVulkanPhysicalDevice();
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
                 "VmaAllocator is nullptr. You must call `VulkanAllocator::Init` first. " );
        }

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.usage                   = usage;

        VmaAllocation  allocation;
        const VkResult resultAllocation = vmaCreateBuffer( s_VmaAllocator, &bufferCreateInfo, &allocCreateInfo,
                                                           &outBuffer, &allocation, nullptr );

        if ( resultAllocation != VK_SUCCESS )
        {
            return Common::MakeError<VmaAllocation>( "error while allocating buffer" );
        }

        VmaAllocationInfo allocInfo;
        vmaGetAllocationInfo( s_VmaAllocator, allocation, &allocInfo );

        const auto& infoData               = MakeNewAllocatedInfoData( tag, allocInfo );
        s_BufferAllocationSizes[outBuffer] = infoData;
        s_TotalAllocatedBytes += allocInfo.size;

        ALLOCATOR_LOG( "Allocating buffer ( {} ); size: {}. ", tag, allocInfo.size );
        ALLOCATOR_LOG( "\tTotal allocated : {}. ", s_TotalAllocatedBytes );

        return Common::MakeSuccess( allocation );
    }

    void VulkanAllocator::RT_DestroyBuffer( VkBuffer buffer, VmaAllocation allocation )
    {
        auto it = s_BufferAllocationSizes.find( buffer );
        if ( it != s_BufferAllocationSizes.end() )
        {
            s_TotalAllocatedBytes -= it->second.Size;
            ALLOCATOR_LOG( "Deallocating buffer ( {} ); size: {}. ", it->second.Tag, it->second.Size );
            ALLOCATOR_LOG( "\tTotal allocated : {}. ", s_TotalAllocatedBytes );

            s_BufferAllocationSizes.erase( it );
        }
        vmaDestroyBuffer( s_VmaAllocator, buffer, allocation );
    }

    void VulkanAllocator::RT_DestroyImage( VkImage image, VmaAllocation allocation )
    {
        auto it = s_ImageAllocationSizes.find( image );
        if ( it != s_ImageAllocationSizes.end() )
        {
            s_TotalAllocatedBytes -= it->second.Size;
            ALLOCATOR_LOG( "Deallocating image ( {} ); size: {}. ", it->second.Tag, it->second.Size );
            ALLOCATOR_LOG( "\tTotal allocated: {}. ", s_TotalAllocatedBytes );

            s_ImageAllocationSizes.erase( it );
        }
        vmaDestroyImage( s_VmaAllocator, image, allocation );
    }

    void VulkanAllocator::Shutdown()
    {
        for ( const auto& img : s_ImageAllocationSizes )
        {
            LOG_ERROR( "{} was not deallocated. Image", img.second.Tag );
        }
        for ( const auto& buff : s_BufferAllocationSizes )
        {
            LOG_ERROR( "{} was not deallocated. Buffer", buff.second.Tag );
        }

        CheckResourceLeaks();

        if ( s_VmaAllocator )
        {
            vmaDestroyAllocator( s_VmaAllocator );
            s_VmaAllocator = nullptr;
        }
    }

    void VulkanAllocator::CheckResourceLeaks()
    {
        CHECK_RESOURCE_LEAKS();
    }
} // namespace Desert::Graphic::API::Vulkan