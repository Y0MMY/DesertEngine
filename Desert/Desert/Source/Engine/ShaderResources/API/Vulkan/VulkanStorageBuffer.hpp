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
        VulkanStorageBuffer( const std::string_view bufferName, uint32_t size, uint32_t binding,
                             bool persistent = false );
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

        // The descriptor for (@p frameIndex x recording renderer slot). The frame comes from the caller
        // and the slot is resolved here, so a write and the descriptor that points at it cannot
        // disagree about the view.
        //
        // @p frameIndex used to be accepted and dropped: this body read the CURRENT frame while
        // VulkanUniformBuffer::GetDescriptorBufferInfo honoured the argument, so the two buffer types
        // answered the same question differently. Latent, because all four call sites
        // (VulkanMaterialBackend x2, VulkanPipelineCompute, VulkanRenderer's indirect draw) pass the
        // current frame — but "the parameter is ignored" is exactly how a caller that finally needs to
        // ask about another frame gets a silently wrong buffer.
        const VkDescriptorBufferInfo& GetDescriptorBufferInfo( uint32_t frameIndex ) const
        {
            return m_DescriptorInfos[CopyIndex( frameIndex )];
        }

        virtual const void* GetData() const override
        {
            return m_LocalStorage.Data;
        }

    private:
        // The copy belonging to (@p frameIndex x recording renderer slot).
        static uint32_t CopyIndex( uint32_t frameIndex );
        // The same, for the frame being recorded now. Writes use this; descriptors take the frame from
        // their caller. Both go through the one arithmetic (ShaderResources::BufferCopyIndex).
        static uint32_t CopyIndex();

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
        // true = ONE device buffer shared by every frame (GPU state persists across frames); false =
        // per-frame-in-flight. See StorageBuffer::Create.
        bool m_Persistent = false;

        Common::Memory::Buffer m_LocalStorage; // CPU shadow copy (for GetData)
    };
} // namespace Desert::ShaderResources::API::Vulkan
