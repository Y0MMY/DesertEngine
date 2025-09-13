#include <Engine/Uniforms/API/Vulkan/VulkanStorageBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>

#include <Engine/Core/EngineContext.hpp>

namespace Desert::Uniforms::API::Vulkan
{

    VulkanStorageBuffer::VulkanStorageBuffer( const std::string_view bufferName, uint32_t size, uint32_t binding )
         : m_Size( size ), m_Binding( binding ), m_BufferName( bufferName )
    {
        RT_Invalidate();
    }

    VulkanStorageBuffer::~VulkanStorageBuffer()
    {
        Release();
    }

    void VulkanStorageBuffer::Release()
    {
        if ( !m_MemoryAlloc )
            return;

        SP_CAST( Desert::Graphic::API::Vulkan::VulkanContext, EngineContext::GetInstance().GetRendererContext() )
             ->GetVulkanAllocator()
             ->RT_DestroyBuffer( m_Buffer, m_MemoryAlloc );
        m_Buffer      = nullptr;
        m_MemoryAlloc = nullptr;
        m_LocalStorage.Release();
    }

    void VulkanStorageBuffer::RT_Invalidate()
    {
        Release();

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext                = nullptr;
        allocInfo.allocationSize       = 0;
        allocInfo.memoryTypeIndex      = 0;

        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.usage              = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.size               = m_Size;

        const auto allocatedBuffer = SP_CAST( Desert::Graphic::API::Vulkan::VulkanContext,
                                              EngineContext::GetInstance().GetRendererContext() )
                                          ->GetVulkanAllocator()
                                          ->RT_AllocateBuffer( std::format( "{}-StorageBuffer", m_BufferName ),
                                                               bufferInfo, VMA_MEMORY_USAGE_CPU_TO_GPU, m_Buffer );

        if ( !allocatedBuffer.IsSuccess() )
        {
            return;
        }
        m_MemoryAlloc = allocatedBuffer.GetValue();

        m_DescriptorInfo.buffer = m_Buffer;
        m_DescriptorInfo.offset = 0;
        m_DescriptorInfo.range  = m_Size;
    }

    void VulkanStorageBuffer::SetData( const void* data, uint32_t size, uint32_t offset )
    {
        if ( size > m_Size )
        {
            m_Size = size;
            RT_Invalidate();
        }

        m_LocalStorage = Common::Memory::Buffer::Copy( data, size );
        RT_SetData( m_LocalStorage.Data, size, offset );
    }

    void VulkanStorageBuffer::RT_SetData( const void* data, uint32_t size, uint32_t offset )
    {
        uint8_t* pData = SP_CAST( Desert::Graphic::API::Vulkan::VulkanContext,
                                  EngineContext::GetInstance().GetRendererContext() )
                              ->GetVulkanAllocator()
                              ->MapMemory( m_MemoryAlloc );
        memcpy( pData, (const uint8_t*)data + offset, size );
        SP_CAST( Desert::Graphic::API::Vulkan::VulkanContext, EngineContext::GetInstance().GetRendererContext() )
             ->GetVulkanAllocator()
             ->UnmapMemory( m_MemoryAlloc );
    }

} // namespace Desert::Uniforms::API::Vulkan