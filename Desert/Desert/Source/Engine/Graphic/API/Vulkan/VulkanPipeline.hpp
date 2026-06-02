#pragma once

#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>

#include <vulkan/vulkan.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanPipeline final : public GraphicsPipeline
    {
    public:
        VulkanPipeline( const GraphicsPipelineSpecification& specification );
        ~VulkanPipeline() override;

        virtual void Invalidate() override;
        virtual void Release() override;

        [[nodiscard]] virtual PipelineType GetType() const override { return PipelineType::Graphics; }
        [[nodiscard]] virtual std::shared_ptr<Shader> GetShader() const override { return m_Specification.Shader; }

        [[nodiscard]] virtual const GraphicsPipelineSpecification& GetSpecification() const override
        {
            return m_Specification;
        }

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
        GraphicsPipelineSpecification m_Specification;

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