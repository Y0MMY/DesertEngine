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
        ~VulkanPipeline();

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

    private:
        bool HasDepth();

    private:
        VkStencilOpState ConvertStencilOpState( const StencilOpState& state );

        void CreatePipelineLayout();
        void CreateVertexInputState();
        void CreateInputAssemblyState();
        void CreateDynamicState();
        void CreateViewportState();
        void CreateRasterizationState();
        void CreateMultisampleState();
        void CreateDepthStencilState();
        void CreateColorBlendState();

        void CreateGraphicsPipeline( VkDevice device, VulkanShader* vulkanShader );

    private:
        std::pair<uint32_t, VkPushConstantRange> SetUpPushConstantRange() const;

    private:
        PipelineSpecification m_Specification;

        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        VkPipeline       m_Pipeline= VK_NULL_HANDLE;

        VkPipelineVertexInputStateCreateInfo   m_VertexInputInfo{};
        VkPipelineInputAssemblyStateCreateInfo m_InputAssembly{};
        VkPipelineDynamicStateCreateInfo       m_DynamicStateInfo{};
        VkPipelineViewportStateCreateInfo      m_ViewportState{};
        VkPipelineRasterizationStateCreateInfo m_Rasterizer{};
        VkPipelineMultisampleStateCreateInfo   m_Multisampling{};
        VkPipelineDepthStencilStateCreateInfo  m_DepthStencil{};
        VkPipelineColorBlendStateCreateInfo    m_ColorBlending{};
        VkVertexInputBindingDescription m_VertexInputBinding;

        std::vector<VkVertexInputAttributeDescription>   m_VertexAttributes;
        std::vector<VkDynamicState>                      m_DynamicStates;
        std::vector<VkPipelineColorBlendAttachmentState> m_ColorBlendAttachments;
    };
} // namespace Desert::Graphic::API::Vulkan