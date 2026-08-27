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

        virtual void SetData( const void* data, uint32_t size, uint32_t offset = 0 ) override;

        virtual uint8_t* MapMemory() override;
        virtual void     UnmapMemory() override;

        // The descriptor for (@p frameIndex x RECORDING RENDERER). The slot is resolved here rather than
        // passed in: every caller wants the copy for the renderer that is recording, and threading a
        // second index through the material layer would only create ways to get them out of step. The
        // FRAME, by contrast, is the caller's to choose, and VulkanStorageBuffer now answers it the same
        // way — the two used to disagree about whether this argument meant anything.
        const VkDescriptorBufferInfo& GetDescriptorBufferInfo( uint32_t frameIndex ) const
        {
            return m_DescriptorInfos[CopyIndex( frameIndex )];
        }
        virtual const void* GetData() const override
        {
            return nullptr;
        }

    private:
        void Release();
        void RT_Invalidate();

        // Copies are laid out frame-major: [frame][slot]. Two views writing the same buffer is exactly
        // what made the viewport lose its shadows to a preview (Docs/RENDERER_FRAME_STATE.md).
        static uint32_t CopyIndex( uint32_t frameIndex );

    private:
        std::vector<VmaAllocation>          m_MemoryAllocs;
        std::vector<VkBuffer>               m_Buffers;
        std::vector<VkDescriptorBufferInfo> m_DescriptorInfos;
        std::vector<uint8_t*>               m_MappedMemories;
    };
} // namespace Desert::ShaderResources::API::Vulkan