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

        const uint32_t framesInFlight = EngineContext::GetInstance().GetMaxFramesInFlight();

        m_Buffers.resize( framesInFlight, VK_NULL_HANDLE );
        m_MemoryAllocs.resize( framesInFlight, nullptr );
        m_DescriptorInfos.resize( framesInFlight );
        m_MappedMemories.resize( framesInFlight, nullptr );

        auto vulkanContext = SP_CAST( Desert::Graphic::API::Vulkan::VulkanContext,
                                      EngineContext::GetInstance().GetRendererContext() );

        for ( uint32_t i = 0; i < framesInFlight; ++i )
        {
            VkBufferCreateInfo bufferInfo = {};
            bufferInfo.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.usage              = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            bufferInfo.size               = m_UniformModel.Size;

            const auto allocatedBuffer = vulkanContext->GetVulkanAllocator()->RT_AllocateBuffer(
                 std::format( "{}-UniformBuffer-Frame{}", m_UniformModel.Name, i ), bufferInfo,
                 VMA_MEMORY_USAGE_CPU_TO_GPU, m_Buffers[i] );

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
        const uint32_t frameIndex = EngineContext::GetInstance().GetCurrentFrameIndex();

        if ( frameIndex >= m_MappedMemories.size() || !m_MappedMemories[frameIndex] )
        {
            return;
        }

        memcpy( m_MappedMemories[frameIndex] + offset, data, size );
    }

    uint8_t* VulkanUniformBuffer::MapMemory()
    {
        const uint32_t frameIndex = EngineContext::GetInstance().GetCurrentFrameIndex();

        if ( frameIndex >= m_MappedMemories.size() )
        {
            return nullptr;
        }

        if ( m_MappedMemories[frameIndex] )
        {
            return m_MappedMemories[frameIndex];
        }

        m_MappedMemories[frameIndex] = SP_CAST( Desert::Graphic::API::Vulkan::VulkanContext,
                                                EngineContext::GetInstance().GetRendererContext() )
                                            ->GetVulkanAllocator()
                                            ->MapMemory( m_MemoryAllocs[frameIndex] );

        return m_MappedMemories[frameIndex];
    }

    void VulkanUniformBuffer::UnmapMemory()
    {
        // Buffers are persistently mapped until Release(); this call is intentionally a no-op.
    }

} // namespace Desert::ShaderResources::API::Vulkan