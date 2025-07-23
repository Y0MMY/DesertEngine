#pragma once

#include <Engine/Uniforms/UniformImageCube.hpp>
#include <Common/Core/Memory/Buffer.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanAllocator.hpp>

namespace Desert::Uniforms::API::Vulkan
{
    class VulkanUniformImageCube : public UniformImageCube
    {
    public:
        VulkanUniformImageCube( const std::string_view debugName, uint32_t binding );
        virtual ~VulkanUniformImageCube();

        virtual const uint32_t GetBinding() const override
        {
            return m_Binding;
        }

        const VkDescriptorImageInfo& GetDescriptorImageInfo() const
        {
            return m_DescriptorInfo;
        }

        virtual const Common::UUID GetImageHash() const override
        {
            return m_ImageCube->GetHash();
        }

        virtual void SetImageCube( const std::shared_ptr<Graphic::ImageCube>& imageCube );

    private:
        void RT_Invalidate();
        void Release();

    private:
        VkDescriptorImageInfo               m_DescriptorInfo{};
        const std::string                   m_DebugName;
        uint32_t                            m_Binding = 0;
        std::shared_ptr<Graphic::ImageCube> m_ImageCube;
    };
} // namespace Desert::Uniforms::API::Vulkan