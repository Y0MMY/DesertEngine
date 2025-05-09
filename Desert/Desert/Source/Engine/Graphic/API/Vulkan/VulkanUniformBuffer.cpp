#include <Engine/Graphic/API/Vulkan/VulkanUniformBuffer.hpp>

namespace Desert::Graphic::API::Vulkan
{

    VulkanUniformBuffer::VulkanUniformBuffer( uint32_t size, uint32_t binding )
         : m_Size( size ), m_Binding( binding )
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

        m_Buffer      = nullptr;
        m_MemoryAlloc = nullptr;

        m_LocalStorage.Release();
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
        bufferInfo.size               = m_Size;

        const auto allocatedBuffer = VulkanAllocator::GetInstance().RT_AllocateBuffer(
             "UniformBuffer", bufferInfo, VMA_MEMORY_USAGE_CPU_TO_GPU, m_Buffer );

        if ( !allocatedBuffer.IsSuccess() )
        {
            return;
        }
        m_MemoryAlloc = allocatedBuffer.GetValue();

        m_DescriptorInfo.buffer = m_Buffer;
        m_DescriptorInfo.offset = 0;
        m_DescriptorInfo.range  = m_Size;
    }

    void VulkanUniformBuffer::SetData( const void* data, uint32_t size, uint32_t offset )
    {
        m_LocalStorage = Common::Memory::Buffer::Copy( data, size );
        RT_SetData( m_LocalStorage.Data, size, offset );
    }

    void VulkanUniformBuffer::RT_SetData( const void* data, uint32_t size, uint32_t offset )
    {
        uint8_t* pData = VulkanAllocator::GetInstance().MapMemory( m_MemoryAlloc );
        memcpy( pData, (const uint8_t*)data + offset, size );
        VulkanAllocator::GetInstance().UnmapMemory( m_MemoryAlloc );
    }

    //Common::Result<std::shared_ptr<Desert::Graphic::UniformBuffer>>
    //VulkanUniformBufferManager::GetUniformBuffer( const std::string& name ) const
    //{
    //    const auto index        = m_UniformBuffers.size();
    //    m_UniformBuffers[index] = std::move( buffer );
    //    m_NameMap[name]         = index;

    //    auto it = m_NameMap.find( name );
    //    if ( it == m_NameMap.end() )
    //    {

    //        return Common::MakeFormattedError<std::shared_ptr<UniformBuffer>>(
    //             "Uniform '{}' not found in material", name );
    //    }
    //}

} // namespace Desert::Graphic::API::Vulkan