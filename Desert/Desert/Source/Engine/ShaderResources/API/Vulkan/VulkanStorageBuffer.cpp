#include <Engine/ShaderResources/API/Vulkan/VulkanStorageBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>
#include <Engine/Core/EngineContext.hpp>

#include <cstring>

namespace Desert::ShaderResources::API::Vulkan
{
    VulkanStorageBuffer::VulkanStorageBuffer( const std::string_view bufferName, uint32_t size, uint32_t binding,
                                              bool persistent )
         : m_Size( size ), m_Binding( binding ), m_BufferName( bufferName ), m_Persistent( persistent )
    {
        if ( m_Size == 0 )
            m_Size = 1; // Vulkan requires size > 0; grows on first SetData.
        RT_Invalidate();
    }

    VulkanStorageBuffer::~VulkanStorageBuffer()
    {
        Release();
    }

    void VulkanStorageBuffer::Release()
    {
        if ( m_Buffers.empty() )
            return;

        auto allocator = SP_CAST( Desert::Graphic::API::Vulkan::VulkanContext,
                                  EngineContext::GetInstance().GetRendererContext() )
                              ->GetVulkanAllocator()
                              .get();

        for ( uint32_t i = 0; i < static_cast<uint32_t>( m_Buffers.size() ); ++i )
        {
            if ( m_MappedMemories[i] )
            {
                allocator->UnmapMemory( m_MemoryAllocs[i] );
                m_MappedMemories[i] = nullptr;
            }
            if ( m_MemoryAllocs[i] )
            {
                // Deferred destroy: in-flight frames may still reference the old buffer.
                allocator->RT_DestroyBuffer( m_Buffers[i], m_MemoryAllocs[i] );
                m_Buffers[i]      = VK_NULL_HANDLE;
                m_MemoryAllocs[i] = nullptr;
            }
        }

        m_Buffers.clear();
        m_MemoryAllocs.clear();
        m_DescriptorInfos.clear();
        m_MappedMemories.clear();
    }

    void VulkanStorageBuffer::RT_Invalidate()
    {
        Release();

        const uint32_t framesInFlight = EngineContext::GetInstance().GetMaxFramesInFlight();
        // Persistent = ONE buffer shared by every frame (GPU sim state must survive across frames); otherwise
        // one buffer per frame in flight. The descriptor-info array stays sized to framesInFlight either way,
        // so GetDescriptorBufferInfo(frameIndex) is always valid — persistent just points them all at buffer 0.
        const uint32_t bufferCount = m_Persistent ? 1u : framesInFlight;

        m_Buffers.resize( bufferCount, VK_NULL_HANDLE );
        m_MemoryAllocs.resize( bufferCount, nullptr );
        m_MappedMemories.resize( bufferCount, nullptr );
        m_DescriptorInfos.resize( framesInFlight );

        auto vulkanContext = SP_CAST( Desert::Graphic::API::Vulkan::VulkanContext,
                                      EngineContext::GetInstance().GetRendererContext() );

        for ( uint32_t i = 0; i < bufferCount; ++i )
        {
            VkBufferCreateInfo bufferInfo = {};
            bufferInfo.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            // INDIRECT lets a storage buffer also serve as a vkCmdDrawIndirect args source (GPU-culled
            // grass writes its draw count here). Harmless for storage buffers never used indirectly.
            bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
            bufferInfo.size  = m_Size;

            const auto allocatedBuffer = vulkanContext->GetVulkanAllocator()->RT_AllocateBuffer(
                 std::format( "{}-StorageBuffer-{}{}", m_BufferName, m_Persistent ? "Persistent" : "Frame", i ),
                 bufferInfo, VMA_MEMORY_USAGE_CPU_TO_GPU, m_Buffers[i] );

            if ( !allocatedBuffer.IsSuccess() )
                continue;

            m_MemoryAllocs[i]   = allocatedBuffer.GetValue();
            m_MappedMemories[i] = vulkanContext->GetVulkanAllocator()->MapMemory( m_MemoryAllocs[i] );
        }

        // Point every frame's descriptor at its buffer (persistent: all at buffer 0).
        for ( uint32_t f = 0; f < framesInFlight; ++f )
        {
            const uint32_t idx          = m_Persistent ? 0u : f;
            m_DescriptorInfos[f].buffer = ( idx < m_Buffers.size() ) ? m_Buffers[idx] : VK_NULL_HANDLE;
            m_DescriptorInfos[f].offset = 0;
            m_DescriptorInfos[f].range  = m_Size;
        }
    }

    void VulkanStorageBuffer::SetData( const void* data, uint32_t size, uint32_t offset )
    {
        // Grow (recreating all per-frame buffers) when the write exceeds the current capacity.
        if ( size + offset > m_Size )
        {
            m_Size = size + offset;
            RT_Invalidate();
        }

        if ( !m_LocalStorage.Data || m_LocalStorage.GetAllocatedSize() < size + offset )
            m_LocalStorage.Allocate( m_Size );
        std::memcpy( static_cast<uint8_t*>( m_LocalStorage.Data ) + offset, data, size );

        // Persistent buffers have a single mapping (index 0); per-frame buffers write the current frame's.
        const uint32_t idx = m_Persistent ? 0u : EngineContext::GetInstance().GetCurrentFrameIndex();
        if ( idx < m_MappedMemories.size() && m_MappedMemories[idx] )
            std::memcpy( m_MappedMemories[idx] + offset, data, size );
    }

    uint8_t* VulkanStorageBuffer::MapMemory()
    {
        const uint32_t idx = m_Persistent ? 0u : EngineContext::GetInstance().GetCurrentFrameIndex();
        return idx < m_MappedMemories.size() ? m_MappedMemories[idx] : nullptr;
    }

    void VulkanStorageBuffer::UnmapMemory()
    {
        // Persistently mapped until Release(); intentionally a no-op.
    }

} // namespace Desert::ShaderResources::API::Vulkan
