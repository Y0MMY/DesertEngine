#include <Engine/ShaderResources/API/Vulkan/VulkanStorageBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>
#include <Engine/Core/EngineContext.hpp>

namespace Desert::ShaderResources::API::Vulkan
{

    VulkanStorageBuffer::VulkanStorageBuffer( const std::string_view bufferName, uint32_t size, uint32_t binding ) //TODO: ctr params: StorageBuffer
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

    static uint64_t Align( uint64_t value, uint64_t alignment )
    {
        return ( value + alignment - 1 ) & ~( alignment - 1 );
    }

    void VulkanStorageBuffer::SetData( const void* data, uint32_t size, uint32_t offset )
    {

        const auto& caps =
             SP_CAST( Graphic::API::Vulkan::VulkanLogicalDevice, EngineContext::GetInstance().GetMainDevice() )
                  ->GetPhysicalDevice()
                  ->GetRendererCaps();

        DESERT_VERIFY( offset % caps.StorageBufferAlignment == 0,
                       "StorageBuffer '{}' offset {} is not aligned to {}", m_BufferName, offset,
                       caps.StorageBufferAlignment );

        DESERT_VERIFY( size + offset <= caps.MaxStorageBufferSize, "StorageBuffer '{}' write exceeds device limit",
                       m_BufferName );

        if ( size + offset > m_Size )
        {
            m_Size = Align( size + offset, caps.StorageBufferAlignment );
            RT_Invalidate();
        }

        if ( !m_LocalStorage.Data || m_LocalStorage.Size < size + offset )
        {
            m_LocalStorage.Allocate( m_Size );
        }

        memcpy( (uint8_t*)m_LocalStorage.Data + offset, data, size );

        uint8_t* mapped = MapMemory();
        memcpy( mapped + offset, data, size );
        UnmapMemory();
    }

    uint8_t* VulkanStorageBuffer::MapMemory()
    {
        return SP_CAST( Desert::Graphic::API::Vulkan::VulkanContext,
                        EngineContext::GetInstance().GetRendererContext() )
             ->GetVulkanAllocator()
             ->MapMemory( m_MemoryAlloc );
    }

    void VulkanStorageBuffer::UnmapMemory()
    {
        SP_CAST( Desert::Graphic::API::Vulkan::VulkanContext, EngineContext::GetInstance().GetRendererContext() )
             ->GetVulkanAllocator()
             ->UnmapMemory( m_MemoryAlloc );
    }

} // namespace Desert::ShaderResources::API::Vulkan