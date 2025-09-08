#pragma once

#include <Engine/Uniforms/StorageBuffer.hpp>
#include <Common/Core/Memory/Buffer.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanAllocator.hpp>

namespace Desert::Uniforms::API::Vulkan
{
    class VulkanStorageBuffer : public StorageBuffer
    {
    public:
        VulkanStorageBuffer( const std::string_view bufferName, uint32_t size, uint32_t binding );
        virtual ~VulkanStorageBuffer();

        virtual void           SetData( const void* data, uint32_t size, uint32_t offset = 0 ) override;
        virtual void           RT_SetData( const void* data, uint32_t size, uint32_t offset = 0 ) override;
        virtual const uint32_t GetBinding() const override
        {
            return m_Binding;
        }

        virtual const uint32_t GetSize() const override
        {
            return m_Size;
        }

        const VkDescriptorBufferInfo& GetDescriptorBufferInfo() const
        {
            return m_DescriptorInfo;
        }

        virtual const void* GetData() const override
        {
            return m_LocalStorage.Data;
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
        const std::string      m_BufferName;

        Common::Memory::Buffer m_LocalStorage;
    };
} // namespace Desert::Uniforms::API::Vulkan