#pragma once

#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>

#include <vulkan/vulkan.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanPipelineCompute final : public PipelineCompute
    {
    public:
        VulkanPipelineCompute( const std::shared_ptr<Shader> shader );

        virtual void Begin() override;
        virtual void End() override;

        void Execute(VkDescriptorSet descriptorSet);

        virtual void Invalidate() override;

    private:
        std::shared_ptr<Shader> m_Shader;
        VkPipeline              m_ComputePipeline;
        VkPipelineLayout        m_ComputePipelineLayout;
        VkPipelineCache         m_PipelineCache;

        VkCommandBuffer m_ActiveComputeCommandBuffer = nullptr;
    };
} // namespace Desert::Graphic::API::Vulkan