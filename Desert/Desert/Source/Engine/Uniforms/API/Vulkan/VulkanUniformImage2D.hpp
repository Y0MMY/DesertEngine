#pragma once

#include <Engine/Uniforms/UniformImage2D.hpp>
#include <Common/Core/Memory/Buffer.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanAllocator.hpp>

namespace Desert::Uniforms::API::Vulkan
{
    class VulkanUniformImage2D : public UniformImage2D
    {
    public:
        VulkanUniformImage2D( const std::string_view debugName, uint32_t binding );
        virtual ~VulkanUniformImage2D();

        virtual const uint32_t GetBinding() const override
        {
            return m_Binding;
        }

        const VkDescriptorImageInfo& GetDescriptorImageInfo() const
        {
            return m_DescriptorInfo;
        }

        virtual void SetImage2D( const std::shared_ptr<Graphic::Image2D>& image2D ) override;

    private:
        void RT_Invalidate();
        void Release();

    private:
        VkDescriptorImageInfo                   m_DescriptorInfo{};
        const std::string                       m_DebugName;
        uint32_t                                m_Binding = 0;
        std::shared_ptr<Graphic::Image2D> m_Image2D;
    };
} // namespace Desert::Uniforms::API::Vulkan