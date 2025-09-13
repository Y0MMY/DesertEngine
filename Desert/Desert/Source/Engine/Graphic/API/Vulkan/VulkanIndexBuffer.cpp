#include <Engine/Graphic/API/Vulkan/VulkanIndexBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>

#include <Engine/Core/EngineContext.hpp>

namespace Desert::Graphic::API::Vulkan
{
    namespace
    {
        static VkBufferCreateInfo CreateIndexBufferInfo( uint32_t size, VkBufferUsageFlags flags,
                                                         VkSharingMode sharingMode )
        {
            VkBufferCreateInfo vertexBufferCreateInfo = {};
            vertexBufferCreateInfo.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            vertexBufferCreateInfo.size               = size;
            vertexBufferCreateInfo.usage              = flags;
            vertexBufferCreateInfo.sharingMode        = sharingMode;

            return vertexBufferCreateInfo;
        }
    } // namespace

    VulkanIndexBuffer::VulkanIndexBuffer( const void* data, uint32_t size,
                                          BufferUsage usage /*= BufferUsage::Static */ )
         : m_Size( size ), m_Usage( usage )
    {
        m_StorageBuffer = Common::Memory::Buffer::Copy( data, size );
    }

    VulkanIndexBuffer::VulkanIndexBuffer( uint32_t size, BufferUsage usage /*= BufferUsage::Dynamic */ )
         : m_Size( size ), m_Usage( usage )
    {
        m_StorageBuffer.Allocate( size );
    }

    void VulkanIndexBuffer::SetData()
    {
    }

    void VulkanIndexBuffer::Use( BindUsage use /*= BindUsage::Bind */ ) const
    {
    }

    void VulkanIndexBuffer::RT_Use( BindUsage use /*= BindUsage::Bind */ ) const
    {
    }

    Common::BoolResult VulkanIndexBuffer::Invalidate()
    {
        return RT_Invalidate();
    }

    Common::BoolResult VulkanIndexBuffer::RT_Invalidate()
    {
        VkDevice device = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetMainDevice() )
                               ->GetVulkanLogicalDevice();

        auto allocator = SP_CAST( VulkanContext, EngineContext::GetInstance().GetRendererContext() )
                              ->GetVulkanAllocator()
                              .get();

        if ( m_StorageBuffer.Data == nullptr ) [[unlikely]]
        {
            auto vertexBufferCreateInfo =
                 CreateIndexBufferInfo( m_Size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE );
            m_MemoryAllocation = allocator
                                      ->RT_AllocateBuffer( "VertexBuffer", vertexBufferCreateInfo,
                                                           VMA_MEMORY_USAGE_CPU_TO_GPU, m_VulkanBuffer )
                                      .GetValue();
        }

        else [[likely]]
        {

            VkBufferCreateInfo stagingBufferCreateInfo =
                 CreateIndexBufferInfo( m_Size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_EXCLUSIVE );

            VkBuffer stagingBuffer;
            auto     stagingBufferAllocation = allocator->RT_AllocateBuffer(
                 "IndexBuffer_staging", stagingBufferCreateInfo, VMA_MEMORY_USAGE_CPU_TO_GPU, stagingBuffer );

            if ( !stagingBufferAllocation.IsSuccess() )
            {
                return Common::MakeError<bool>( stagingBufferAllocation.GetError() );
            }

            auto stagingBufferAllocationVAL = stagingBufferAllocation.GetValue();

            // copy data to staging buffer

            auto destData = allocator->MapMemory( stagingBufferAllocationVAL );
            memcpy( destData, m_StorageBuffer.Data, m_StorageBuffer.Size );
            allocator->UnmapMemory( stagingBufferAllocationVAL );

            auto vertexBufferCreateInfo = CreateIndexBufferInfo(
                 m_Size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_SHARING_MODE_EXCLUSIVE );

            const auto buffer = allocator->RT_AllocateBuffer( "IndexBuffer", vertexBufferCreateInfo,
                                                              VMA_MEMORY_USAGE_GPU_ONLY, m_VulkanBuffer );
            if ( !buffer.IsSuccess() )
            {
                return Common::MakeError<bool>( buffer.GetError() );
            }

            m_MemoryAllocation = buffer.GetValue();
            auto copyCmd       = CommandBufferAllocator::GetInstance().RT_AllocateCommandBufferGraphic( true );
            if ( !copyCmd.IsSuccess() )
            {
                return Common::MakeError<bool>( copyCmd.GetError() );
            }

            auto copyCmdVal = copyCmd.GetValue();

            VkBufferCopy copyRegion = {};
            copyRegion.size         = m_Size;

            vkCmdCopyBuffer( copyCmdVal, stagingBuffer, m_VulkanBuffer, 1, &copyRegion );

            CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( copyCmdVal );

            allocator->RT_DestroyBuffer( stagingBuffer, stagingBufferAllocationVAL );
        }
        return Common::MakeSuccess( true );
    }

    VulkanIndexBuffer::~VulkanIndexBuffer()
    {
        Release();
    }

    Common::BoolResult VulkanIndexBuffer::Release()
    {
        m_StorageBuffer.Release();

        SP_CAST( VulkanContext, EngineContext::GetInstance().GetRendererContext() )
             ->GetVulkanAllocator()
             ->RT_DestroyBuffer( m_VulkanBuffer, m_MemoryAllocation );
        m_VulkanBuffer     = nullptr;
        m_MemoryAllocation = nullptr;
        return BOOLSUCCESS;
    }

} // namespace Desert::Graphic::API::Vulkan