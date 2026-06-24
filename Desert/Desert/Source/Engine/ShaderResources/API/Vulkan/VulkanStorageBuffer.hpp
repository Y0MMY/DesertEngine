#pragma once

#include <Engine/ShaderResources/StorageBuffer.hpp>
#include <Common/Core/Memory/Buffer.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanAllocator.hpp>

#include <vector>

namespace Desert::ShaderResources::API::Vulkan
{
    // Per-frame-in-flight storage buffer. Like VulkanUniformBuffer, it keeps one persistently-mapped
    // buffer per frame in flight so the CPU can write next frame's data while the GPU reads the current
    // one without a race (single-buffering here caused exactly the flicker we fixed for uniform buffers).
    class VulkanStorageBuffer : public StorageBuffer
    {
    public:
        VulkanStorageBuffer( const std::string_view bufferName, uint32_t size, uint32_t binding );
        virtual ~VulkanStorageBuffer();

        virtual uint8_t* MapMemory() override;
        virtual void     UnmapMemory() override;

        virtual void           SetData( const void* data, uint32_t size, uint32_t offset = 0 ) override;
        virtual const uint32_t GetBinding() const override
        {
            return m_Binding;
        }

        virtual const uint32_t GetSize() const override
        {
            return m_Size;
        }

        const VkDescriptorBufferInfo& GetDescriptorBufferInfo( uint32_t frameIndex ) const
        {
            return m_DescriptorInfos[frameIndex];
        }

        virtual const void* GetData() const override
        {
            return m_LocalStorage.Data;
        }

    private:
        void Release();
        void RT_Invalidate();

    private:
        std::vector<VmaAllocation>          m_MemoryAllocs;
        std::vector<VkBuffer>               m_Buffers;
        std::vector<VkDescriptorBufferInfo> m_DescriptorInfos;
        std::vector<uint8_t*>               m_MappedMemories;

        uint32_t          m_Size    = 0;
        uint32_t          m_Binding = 0;
        const std::string m_BufferName;

        Common::Memory::Buffer m_LocalStorage; // CPU shadow copy (for GetData)
    };
} // namespace Desert::ShaderResources::API::Vulkan
