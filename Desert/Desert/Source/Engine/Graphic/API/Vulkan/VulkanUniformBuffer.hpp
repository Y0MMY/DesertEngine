#pragma once

#include <Engine/Graphic/UniformBuffer.hpp>
#include <Common/Core/Memory/Buffer.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanAllocator.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanUniformBuffer : public UniformBuffer
    {
    public:
        VulkanUniformBuffer( uint32_t size, uint32_t binding );
        virtual ~VulkanUniformBuffer();

        virtual void     SetData( const void* data, uint32_t size, uint32_t offset = 0 ) override;
        virtual void     RT_SetData( const void* data, uint32_t size, uint32_t offset = 0 ) override;
        virtual uint32_t GetBinding() const override
        {
            return m_Binding;
        }

        const VkDescriptorBufferInfo& GetDescriptorBufferInfo() const
        {
            return m_DescriptorInfo;
        }

    private:
        void Release();
        void RT_Invalidate();

    private:
        VmaAllocation          m_MemoryAlloc = nullptr;
        VkBuffer               m_Buffer;
        VkDescriptorBufferInfo m_DescriptorInfo{};
        uint32_t               m_Size    = 0;
        uint32_t               m_Binding = 0;

        Common::Memory::Buffer m_LocalStorage;
    };
} // namespace Desert::Graphic::API::Vulkan