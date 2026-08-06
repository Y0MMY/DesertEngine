#include "VulkanUniformBuffer.hpp"

#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>

#include <Engine/Core/EngineContext.hpp>

namespace Desert::ShaderResources::API::Vulkan
{

    VulkanUniformBuffer::VulkanUniformBuffer( const ShaderLayout::UniformBuffer& uniform )
         : UniformBuffer( uniform )
    {
        RT_Invalidate();
    }

    VulkanUniformBuffer::~VulkanUniformBuffer()
    {
        Release();
    }

    uint32_t VulkanUniformBuffer::CopyIndex( uint32_t frameIndex )
    {
        const uint32_t slots = EngineContext::kMaxRendererSlots;
        const uint32_t slot  = EngineContext::GetInstance().GetActiveRendererSlot();
        return frameIndex * slots + ( slot < slots ? slot : 0 );
    }

    void VulkanUniformBuffer::Release()
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

    void VulkanUniformBuffer::RT_Invalidate()
    {
        Release();

        // One copy per (frame in flight x renderer slot). The frame dimension keeps a buffer the GPU is
        // still reading from being overwritten; the SLOT dimension keeps a second view from overwriting
        // the first one's camera, lights and shadow state inside the same frame — that is the whole point
        // of this change. A uniform block is a few hundred bytes to a few KB, so the extra copies cost
        // kilobytes per material.
        const uint32_t framesInFlight = EngineContext::GetInstance().GetMaxFramesInFlight();
        const uint32_t copies         = framesInFlight * EngineContext::kMaxRendererSlots;

        m_Buffers.resize( copies, VK_NULL_HANDLE );
        m_MemoryAllocs.resize( copies, nullptr );
        m_DescriptorInfos.resize( copies );
        m_MappedMemories.resize( copies, nullptr );

        auto vulkanContext = SP_CAST( Desert::Graphic::API::Vulkan::VulkanContext,
                                      EngineContext::GetInstance().GetRendererContext() );

        for ( uint32_t i = 0; i < copies; ++i )
        {
            VkBufferCreateInfo bufferInfo = {};
            bufferInfo.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.usage              = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            bufferInfo.size               = m_UniformModel.Size;

            const auto allocatedBuffer = vulkanContext->GetVulkanAllocator()->RT_AllocateBuffer(
                 std::format( "{}-UniformBuffer-Frame{}-Slot{}", m_UniformModel.Name,
                              i / EngineContext::kMaxRendererSlots, i % EngineContext::kMaxRendererSlots ),
                 bufferInfo, VMA_MEMORY_USAGE_CPU_TO_GPU, m_Buffers[i] );

            if ( !allocatedBuffer.IsSuccess() )
            {
                continue;
            }
            m_MemoryAllocs[i] = allocatedBuffer.GetValue();

            m_DescriptorInfos[i].buffer = m_Buffers[i];
            m_DescriptorInfos[i].offset = 0;
            m_DescriptorInfos[i].range  = m_UniformModel.Size;

            // Persistently map for the lifetime of this buffer (CPU_TO_GPU stays mappable).
            // UnmapMemory() is a no-op; the actual unmap happens in Release().
            m_MappedMemories[i] = vulkanContext->GetVulkanAllocator()->MapMemory( m_MemoryAllocs[i] );
            if ( m_MappedMemories[i] )
                std::memset( m_MappedMemories[i], 0, m_UniformModel.Size );
        }
    }

    void VulkanUniformBuffer::SetData( const void* data, uint32_t size, uint32_t offset )
    {
        const uint32_t index = CopyIndex( EngineContext::GetInstance().GetCurrentFrameIndex() );

        if ( index >= m_MappedMemories.size() || !m_MappedMemories[index] )
        {
            return;
        }

        memcpy( m_MappedMemories[index] + offset, data, size );
    }

    uint8_t* VulkanUniformBuffer::MapMemory()
    {
        const uint32_t index = CopyIndex( EngineContext::GetInstance().GetCurrentFrameIndex() );

        if ( index >= m_MappedMemories.size() )
        {
            return nullptr;
        }

        if ( m_MappedMemories[index] )
        {
            return m_MappedMemories[index];
        }

        m_MappedMemories[index] = SP_CAST( Desert::Graphic::API::Vulkan::VulkanContext,
                                           EngineContext::GetInstance().GetRendererContext() )
                                       ->GetVulkanAllocator()
                                       ->MapMemory( m_MemoryAllocs[index] );

        return m_MappedMemories[index];
    }

    void VulkanUniformBuffer::UnmapMemory()
    {
        // Buffers are persistently mapped until Release(); this call is intentionally a no-op.
    }

} // namespace Desert::ShaderResources::API::Vulkan