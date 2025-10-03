#pragma once

#include <Engine/Graphic/IndexBuffer.hpp>
#include <Common/Core/Memory/Buffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>

#include <VulkanAllocator/vk_mem_alloc.h>

#include <vulkan/vulkan.h>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanIndexBuffer : public IndexBuffer
    {
    public:
        VulkanIndexBuffer( const void* data, uint32_t size, BufferUsage usage = BufferUsage::Static );
        VulkanIndexBuffer( uint32_t size, BufferUsage usage = BufferUsage::Dynamic );

        virtual ~VulkanIndexBuffer();
        virtual void SetData() override;
        virtual void Use( BindUsage use = BindUsage::Bind ) const override;
        virtual void RT_Use( BindUsage use = BindUsage::Bind ) const override;

        [[nodiscard]] virtual Common::BoolResultStr Invalidate() override;
        [[nodiscard]] virtual Common::BoolResultStr RT_Invalidate() override;
        [[nodiscard]] virtual Common::BoolResultStr Release() override;

        [[nodiscard]] virtual unsigned int GetSize() const override
        {
            return m_Size;
        }
        [[nodiscard]] virtual unsigned int GetCount() const override
        {
            return m_Size / sizeof( uint32_t );
        }

        VkBuffer GetVulkanBuffer()
        {
            return m_VulkanBuffer;
        }

    private:
        uint32_t               m_Size = 0;
        BufferUsage            m_Usage;
        Common::Memory::Buffer m_StorageBuffer;

        VkBuffer      m_VulkanBuffer = nullptr;
        VmaAllocation m_MemoryAllocation;
    };
} // namespace Desert::Graphic::API::Vulkan