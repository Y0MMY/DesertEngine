#pragma once

#include <Engine/Graphic/VertexBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>
#include <Common/Core/Memory/Buffer.hpp>

#include <VulkanAllocator/vk_mem_alloc.h>

#include <vulkan/vulkan.h>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanVertexBuffer : public VertexBuffer
    {
    public:
        VulkanVertexBuffer( void* data, uint32_t size, BufferUsage usage = BufferUsage::Static );
        VulkanVertexBuffer( uint32_t size, BufferUsage usage = BufferUsage::Dynamic );

        virtual ~VulkanVertexBuffer() = default;
        virtual void SetData( void* data, uint32_t size, uint32_t offset = 0 ) override;
        virtual void Use( BindUsage use = BindUsage::Bind ) const override;
        virtual void RT_Use( BindUsage use = BindUsage::Bind ) const override;

        [[nodiscard]] Common::BoolResult Invalidate( const std::shared_ptr<VulkanLogicalDevice>& device );
        [[nodiscard]] Common::BoolResult RT_Invalidate( const std::shared_ptr<VulkanLogicalDevice>& device );

        virtual unsigned int GetSize() const override
        {
            return m_Size;
        }

        VkBuffer GetVulkanBuffer()
        {
            return m_VulkanBuffer;
        }

    private:
        uint32_t    m_Size = 0;
        BufferUsage m_Usage;

        VkBuffer               m_VulkanBuffer;
        VmaAllocation          m_MemoryAllocation;
        Common::Memory::Buffer m_StorageBuffer;
    };
} // namespace Desert::Graphic::API::Vulkan