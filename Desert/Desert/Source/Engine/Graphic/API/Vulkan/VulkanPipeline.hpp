#pragma once

#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>

#include <vulkan/vulkan.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanPipeline final : public Pipeline
    {
    public:
        VulkanPipeline( const PipelineSpecification& specification );

        virtual const PipelineSpecification GetSpecification() const override
        {
            return m_Specification;
        }

        virtual void Invalidate() override;

        const VkPipeline GetVkPipeline() const
        {
            return m_Pipeline;
        }

        const VkPipelineLayout GetVkPipelineLayout() const
        {
            return m_PipelineLayout;
        }

        const auto GetShaderDescriptorSet() const
        {
            return m_ShaderDescriptorSet;
        }

    private:
        PipelineSpecification m_Specification;

        VkPipelineLayout m_PipelineLayout;
        VkPipeline       m_Pipeline;

        VulkanShader::ShaderDescriptorSet m_ShaderDescriptorSet;
    };
} // namespace Desert::Graphic::API::Vulkan