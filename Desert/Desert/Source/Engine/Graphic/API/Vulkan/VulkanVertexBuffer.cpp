#include <Engine/Graphic/API/Vulkan/VulkanVertexBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanAllocator.hpp>

namespace Desert::Graphic::API::Vulkan
{

    namespace
    {
        static VkBufferCreateInfo CreateVertexBufferInfo( uint32_t size, VkBufferUsageFlags flags,
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

    void VulkanVertexBuffer::SetData( void* data, uint32_t size, uint32_t offset /*= 0 */ )
    {
    }

    void VulkanVertexBuffer::Use( BindUsage use /*= BindUsage::Bind */ ) const
    {
    }

    void VulkanVertexBuffer::RT_Use( BindUsage use /*= BindUsage::Bind */ ) const
    {
    }

    [[nodiscard]] Common::BoolResult
    VulkanVertexBuffer::Invalidate( const std::shared_ptr<VulkanLogicalDevice>& device )
    {
        return RT_Invalidate( device );
    }

    [[nodiscard]] Common::BoolResult
    VulkanVertexBuffer::RT_Invalidate( const std::shared_ptr<VulkanLogicalDevice>& device )
    {
        auto& allocator = VulkanAllocator::GetInstance();

        if ( m_StorageBuffer.Data == nullptr ) [[unlikely]]
        {
            auto vertexBufferCreateInfo =
                 CreateVertexBufferInfo( m_Size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE );
            m_MemoryAllocation = allocator
                                      .RT_AllocateBuffer( "VertexBuffer", vertexBufferCreateInfo,
                                                          VMA_MEMORY_USAGE_CPU_TO_GPU, m_VulkanBuffer )
                                      .GetValue();
        }

        else [[likely]]
        {

            VkBufferCreateInfo stagingBufferCreateInfo =
                 CreateVertexBufferInfo( m_Size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_EXCLUSIVE );

            VkBuffer stagingBuffer;
            auto     stagingBufferAllocation = allocator.RT_AllocateBuffer(
                 "VertexBuffer_staging", stagingBufferCreateInfo, VMA_MEMORY_USAGE_CPU_TO_GPU, stagingBuffer );

            if ( !stagingBufferAllocation.IsSuccess() )
            {
                return Common::MakeError<bool>( stagingBufferAllocation.GetError() );
            }

            auto stagingBufferAllocationVAL = stagingBufferAllocation.GetValue();

            // copy data to staging buffer

            auto destData = allocator.MapMemory( stagingBufferAllocationVAL );
            memcpy( destData, m_StorageBuffer.Data, m_StorageBuffer.Size );
            allocator.UnmapMemory( stagingBufferAllocationVAL );

            auto vertexBufferCreateInfo = CreateVertexBufferInfo(
                 m_Size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_SHARING_MODE_EXCLUSIVE );

            const auto buffer = allocator.RT_AllocateBuffer( "VertexBuffer", vertexBufferCreateInfo,
                                                             VMA_MEMORY_USAGE_GPU_ONLY, m_VulkanBuffer );
            if ( !buffer.IsSuccess() )
            {
                return Common::MakeError<bool>( buffer.GetError() );
            }

            m_MemoryAllocation = buffer.GetValue();
            auto copyCmd       = device->RT_GetCommandBufferGraphic();
            if ( !copyCmd.IsSuccess() )
            {
                return Common::MakeError<bool>( copyCmd.GetError() );
            }

            auto copyCmdVal = copyCmd.GetValue();

            VkBufferCopy copyRegion = {};
            copyRegion.size         = m_Size;

            vkCmdCopyBuffer( copyCmdVal, stagingBuffer, m_VulkanBuffer, 1, &copyRegion );

            device->RT_FlushCommandBufferGraphic( copyCmdVal );

            allocator.RT_DestroyBuffer( stagingBuffer, m_MemoryAllocation );
        }
        return Common::MakeSuccess( true );
    }

    VulkanVertexBuffer::VulkanVertexBuffer( void* data, uint32_t size,
                                            BufferUsage usage /*= BufferUsage::Static */ )
         : m_Size( size ), m_Usage( usage )
    {
        m_StorageBuffer = Common::Memory::Buffer::Copy( data, size );
    }

    VulkanVertexBuffer::VulkanVertexBuffer( uint32_t size, BufferUsage usage /*= BufferUsage::Dynamic */ )
         : m_Size( size ), m_Usage( usage )
    {
        m_StorageBuffer.Allocate( size );
    }
} // namespace Desert::Graphic::API::Vulkan