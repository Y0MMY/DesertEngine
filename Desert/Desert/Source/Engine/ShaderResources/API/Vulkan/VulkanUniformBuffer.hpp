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

        const VkDescriptorBufferInfo& GetDescriptorBufferInfo( uint32_t frameIndex ) const
        {
            return m_DescriptorInfos[frameIndex];
        }
        virtual const void* GetData() const override
        {
            return nullptr;
        }

    private:
        void Release();
        void RT_Invalidate();

    private:
        std::vector<VmaAllocation>          m_MemoryAllocs;
        std::vector<VkBuffer>               m_Buffers;
        std::vector<VkDescriptorBufferInfo> m_DescriptorInfos;
        std::vector<uint8_t*>               m_MappedMemories;
    };
} // namespace Desert::ShaderResources::API::Vulkan