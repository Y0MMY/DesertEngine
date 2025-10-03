#pragma once

#include <Engine/Uniforms/UniformBuffer.hpp>
#include <Common/Core/Memory/Buffer.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanAllocator.hpp>

namespace Desert::Uniforms::API::Vulkan
{
    class VulkanUniformBuffer : public UniformBuffer
    {
    public:
        VulkanUniformBuffer( const Core::Models::UniformBuffer& uniform );
        virtual ~VulkanUniformBuffer();

        virtual void SetData( const void* data, uint32_t size, uint32_t offset = 0 ) override;

        virtual uint8_t* MapMemory() override;
        virtual void     UnmapMemory() override;

        const VkDescriptorBufferInfo& GetDescriptorBufferInfo() const
        {
            return m_DescriptorInfo;
        }
        virtual const void* GetData() const override
        {
            return nullptr;
        }
    private:
        void Release();
        void RT_Invalidate();

    private:
        VmaAllocation          m_MemoryAlloc = nullptr;
        VkBuffer               m_Buffer;
        VkDescriptorBufferInfo m_DescriptorInfo{};

        uint8_t*               m_MappedMemmory = nullptr;
    };
} // namespace Desert::Uniforms::API::Vulkan