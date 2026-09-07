#pragma once

#include <Engine/ShaderResources/UniformBuffer.hpp>
#include <Common/Core/Memory/Buffer.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanAllocator.hpp>

#include <vector>

namespace Desert::ShaderResources::API::Vulkan
{
    class VulkanUniformBuffer : public UniformBuffer
    {
    public:
        VulkanUniformBuffer( const ShaderLayout::UniformBuffer& uniform );
        virtual ~VulkanUniformBuffer();

        NO_DISCARD virtual Common::BoolResultStr SetData( const void* data, uint32_t size,
                                                          uint32_t offset = 0 ) override;

        NO_DISCARD virtual Common::BoolResultStr EnsureMapped() override;

        // The descriptor for (@p frameIndex x RECORDING RENDERER). The slot is resolved here rather than
        // passed in: every caller wants the copy for the renderer that is recording, and threading a
        // second index through the material layer would only create ways to get them out of step. The
        // FRAME, by contrast, is the caller's to choose, and VulkanStorageBuffer now answers it the same
        // way — the two used to disagree about whether this argument meant anything.
        const VkDescriptorBufferInfo& GetDescriptorBufferInfo( uint32_t frameIndex ) const
        {
            // Bounds-checked for the reason spelled out on the sibling accessor in
            // VulkanStorageBuffer.hpp: an unbuilt buffer has no descriptor array at all now that
            // RT_Invalidate refuses as a whole, and the unchecked subscript read past the end of it.
            const uint32_t copy = CopyIndex( frameIndex );
            if ( copy >= m_DescriptorInfos.size() )
            {
                static const VkDescriptorBufferInfo none{};
                return none;
            }
            return m_DescriptorInfos[copy];
        }
        virtual const void* GetData() const override
        {
            return nullptr;
        }

    private:
        void Release();

        /// Allocate and map one copy per (frame in flight x renderer slot). Refuses as a whole if any
        /// single copy could not be allocated or mapped.
        ///
        /// ALL OR NOTHING, AND IT USED TO BE NEITHER. This loop answered a failed allocation with a bare
        /// `continue`: the copy stayed VK_NULL_HANDLE, its VkDescriptorBufferInfo stayed zeroed, and the
        /// function then returned as though the buffer were built. A descriptor set written from a
        /// zeroed VkDescriptorBufferInfo is not a diagnostic, it is undefined behaviour in the driver —
        /// and the only thing that ever hinted at it was that one frame in flight rendered wrong.
        NO_DISCARD Common::BoolResultStr RT_Invalidate();

        // Copies are laid out frame-major: [frame][slot]. Two views writing the same buffer is exactly
        // what made the viewport lose its shadows to a preview (Docs/RENDERER_FRAME_STATE.md).
        static uint32_t CopyIndex( uint32_t frameIndex );

    private:
        std::vector<VmaAllocation>          m_MemoryAllocs;
        std::vector<VkBuffer>               m_Buffers;
        std::vector<VkDescriptorBufferInfo> m_DescriptorInfos;
        // One persistent mapping per copy. `MappedMemory` rather than `uint8_t*`: the mapping unmaps
        // itself when this vector is cleared, and nothing here can write through a copy whose map failed.
        std::vector<Desert::Graphic::MappedMemory> m_Mappings;

        // WHY THE REFUSAL IS STORED. The constructor is the only caller of RT_Invalidate and a
        // constructor has no channel, so without this the answer would be produced and dropped one line
        // after it was introduced. Kept, so every later question — SetData, EnsureMapped — answers with
        // the CAUSE ("copy 3 could not be allocated: out of device memory") rather than with a true but
        // useless consequence ("there is no copy 3").
        Common::BoolResultStr m_Built = Common::MakeError<bool>( "uniform buffer has not been built" );
    };
} // namespace Desert::ShaderResources::API::Vulkan