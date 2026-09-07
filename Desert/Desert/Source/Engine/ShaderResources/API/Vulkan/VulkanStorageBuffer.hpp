#pragma once

#include <Engine/ShaderResources/StorageBuffer.hpp>
#include <Engine/ShaderResources/BufferGrowth.hpp>
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

        NO_DISCARD virtual Common::BoolResultStr EnsureMapped() override;

        NO_DISCARD virtual Common::BoolResultStr SetData( const void* data, uint32_t size,
                                                          uint32_t offset = 0 ) override;

        virtual uint32_t GetBinding() const override
        {
            return m_Binding;
        }

        virtual uint32_t GetSize() const override
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

        /// Allocate and map the buffer copies at the CURRENT m_Size. Refuses as a whole if any single
        /// copy could not be allocated or mapped — see VulkanUniformBuffer::RT_Invalidate for the defect
        /// the previous `continue` produced.
        NO_DISCARD Common::BoolResultStr RT_Invalidate();

        /// Re-create at @p newSize, keeping nothing. A DIFFERENT OPERATION FROM WRITING, and that is the
        /// whole point of it having a name: `SetData` used to call RT_Invalidate directly to make room,
        /// which for a persistent buffer threw away the GPU simulation state its own member comment
        /// requires to survive across frames — silently, from the per-frame write path. The decision of
        /// whether a write may reach here at all is ShaderResources::ClassifyBufferWrite.
        NO_DISCARD Common::BoolResultStr Grow( uint32_t newSize );

        /// One buffer shared by every frame and view, rather than one per (frame x slot).
        bool IsPersistent() const
        {
            return m_Lifetime == Persistence::AcrossFrames;
        }

    private:
        std::vector<VmaAllocation>          m_MemoryAllocs;
        std::vector<VkBuffer>               m_Buffers;
        std::vector<VkDescriptorBufferInfo> m_DescriptorInfos;
        // See VulkanUniformBuffer: the mapping owns its own unmap, and cannot be written through unmapped.
        std::vector<Desert::Graphic::MappedMemory> m_Mappings;

        /// AcrossFrames = ONE device buffer shared by every frame AND every view, because the GPU is the
        /// author of its contents; PerFrame = one per (frame in flight x renderer slot). See
        /// StorageBuffer::Create.
        ///
        /// This REPLACES the `bool m_Persistent` that used to sit here, rather than sitting beside it.
        /// The growth decision (ShaderResources::ClassifyBufferWrite) is written in this vocabulary, and
        /// a bool and an enum saying the same thing are two things obliged to agree with nothing
        /// checking that they do — the defect class this project keeps paying for.
        Persistence m_Lifetime = Persistence::PerFrame;

        // The outcome of the last RT_Invalidate. The constructor has no channel, and a buffer that
        // failed to build must answer every later question with the CAUSE — see VulkanUniformBuffer.
        Common::BoolResultStr m_Built = Common::MakeError<bool>( "storage buffer has not been built" );

        uint32_t          m_Size    = 0;
        uint32_t          m_Binding = 0;
        const std::string m_BufferName;

        Common::Memory::Buffer m_LocalStorage; // CPU shadow copy (for GetData)
    };
} // namespace Desert::ShaderResources::API::Vulkan
