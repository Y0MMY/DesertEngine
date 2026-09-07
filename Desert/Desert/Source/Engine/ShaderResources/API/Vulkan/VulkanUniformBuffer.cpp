#include "VulkanUniformBuffer.hpp"

#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>

#include <Engine/Core/EngineContext.hpp>
#include <Engine/ShaderResources/BufferCopyLayout.hpp>

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
        return BufferCopyIndex( frameIndex, EngineContext::GetInstance().GetActiveRendererSlot(),
                                EngineContext::kMaxRendererSlots );
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
            // Unmap BEFORE the buffer is queued for destruction, exactly as before — the mapping's own
            // destructor does it now, so an early return cannot skip it.
            if ( i < m_Mappings.size() )
                m_Mappings[i].Unmap();
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
        m_Mappings.clear();
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
        const uint32_t copies         = BufferCopyCount( framesInFlight, EngineContext::kMaxRendererSlots );

        m_Buffers.resize( copies, VK_NULL_HANDLE );
        m_MemoryAllocs.resize( copies, nullptr );
        m_DescriptorInfos.resize( copies );
        m_Mappings.resize( copies );

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

            // Persistently mapped for the lifetime of this buffer (CPU_TO_GPU stays mappable); the
            // actual unmap happens in Release(), through the mapping's own destructor.
            m_Mappings[i] = vulkanContext->GetVulkanAllocator()->MapMemory( m_MemoryAllocs[i] );

            const auto cleared = m_Mappings[i].Fill( 0, m_UniformModel.Size );
            if ( !cleared.IsSuccess() )
                LOG_ERROR( "[UniformBuffer] '{}' copy {} starts uninitialised: {}", m_UniformModel.Name, i,
                           cleared.GetError() );
        }
    }

    void VulkanUniformBuffer::SetData( const void* data, uint32_t size, uint32_t offset )
    {
        const uint32_t index = CopyIndex( EngineContext::GetInstance().GetCurrentFrameIndex() );

        if ( index >= m_Mappings.size() )
        {
            LOG_ERROR( "[UniformBuffer] '{}' has no copy {} to write ({} exist)", m_UniformModel.Name, index,
                       m_Mappings.size() );
            return;
        }

        const auto wrote = m_Mappings[index].Write( data, size, offset );
        if ( wrote.IsSuccess() )
            return;

        // A COPY THAT NEVER MAPPED WAS ALREADY NAMED, once, by RT_Invalidate — and this runs per frame
        // per material, so repeating it here would bury the log rather than inform it. A refusal from a
        // LIVE mapping is different: it is a size that does not fit, it is new information every time,
        // and it is the write that would previously have run off the end of the buffer.
        if ( m_Mappings[index].IsMapped() )
            LOG_ERROR( "[UniformBuffer] '{}': {}", m_UniformModel.Name, wrote.GetError() );
    }

    Common::BoolResultStr VulkanUniformBuffer::EnsureMapped()
    {
        const uint32_t index = CopyIndex( EngineContext::GetInstance().GetCurrentFrameIndex() );

        if ( index >= m_Mappings.size() )
            return Common::MakeFormattedError<bool>( "uniform buffer '{}' has no copy {} ({} exist)",
                                                     m_UniformModel.Name, index, m_Mappings.size() );

        if ( m_Mappings[index].IsMapped() )
            return BOOLSUCCESS;

        // The retry: RT_Invalidate maps every copy up front, so reaching here means that map failed.
        m_Mappings[index] = SP_CAST( Desert::Graphic::API::Vulkan::VulkanContext,
                                     EngineContext::GetInstance().GetRendererContext() )
                                 ->GetVulkanAllocator()
                                 ->MapMemory( m_MemoryAllocs[index] );

        if ( !m_Mappings[index].IsMapped() )
            return Common::MakeFormattedError<bool>( "uniform buffer '{}' copy {}: {}", m_UniformModel.Name, index,
                                                     m_Mappings[index].GetRefusal() );

        return BOOLSUCCESS;
    }

} // namespace Desert::ShaderResources::API::Vulkan