#include <Engine/Uniforms/API/Vulkan/VulkanUniformBuffer.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>

#include <Engine/Core/EngineContext.hpp>

namespace Desert::Uniforms::API::Vulkan
{

    VulkanUniformBuffer::VulkanUniformBuffer( const Core::Models::UniformBuffer& uniform )
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
        if ( !m_MemoryAlloc )
            return;

        SP_CAST( Desert::Graphic::API::Vulkan::VulkanContext, EngineContext::GetInstance().GetRendererContext() )
             ->GetVulkanAllocator()
             ->RT_DestroyBuffer( m_Buffer, m_MemoryAlloc );
        m_Buffer      = nullptr;
        m_MemoryAlloc = nullptr;
    }

    void VulkanUniformBuffer::RT_Invalidate()
    {
        Release();

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext                = nullptr;
        allocInfo.allocationSize       = 0;
        allocInfo.memoryTypeIndex      = 0;

        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.usage              = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.size               = m_UniformModel.Size;

        const auto allocatedBuffer =
             SP_CAST( Desert::Graphic::API::Vulkan::VulkanContext,
                      EngineContext::GetInstance().GetRendererContext() )
                  ->GetVulkanAllocator()
                  ->RT_AllocateBuffer( std::format( "{}-UniformBuffer", m_UniformModel.Name ), bufferInfo,
                                       VMA_MEMORY_USAGE_CPU_TO_GPU, m_Buffer );

        if ( !allocatedBuffer.IsSuccess() )
        {
            return;
        }
        m_MemoryAlloc = allocatedBuffer.GetValue();

        m_DescriptorInfo.buffer = m_Buffer;
        m_DescriptorInfo.offset = 0;
        m_DescriptorInfo.range  = m_UniformModel.Size;
    }

    void VulkanUniformBuffer::SetData( const void* data, uint32_t size, uint32_t offset )
    {
        if ( !m_MappedMemmory )
        {
            return; // TODO: result
        }

        memcpy( m_MappedMemmory + offset, data, size );
    }

    uint8_t* VulkanUniformBuffer::MapMemory()
    {
        if ( m_MappedMemmory )
        {
            return m_MappedMemmory;
        }

        m_MappedMemmory = SP_CAST( Desert::Graphic::API::Vulkan::VulkanContext,
                                   EngineContext::GetInstance().GetRendererContext() )
                               ->GetVulkanAllocator()
                               ->MapMemory( m_MemoryAlloc );

        return m_MappedMemmory;
    }

    void VulkanUniformBuffer::UnmapMemory()
    {
        SP_CAST( Desert::Graphic::API::Vulkan::VulkanContext, EngineContext::GetInstance().GetRendererContext() )
             ->GetVulkanAllocator()
             ->UnmapMemory( m_MemoryAlloc );

        m_MappedMemmory = nullptr;
    }

} // namespace Desert::Uniforms::API::Vulkan