#pragma once

#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>

#include <vulkan/vulkan.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanPipelineCompute final : public PipelineCompute
    {
    public:
        VulkanPipelineCompute( const std::shared_ptr<Shader>& shader );

        virtual void Begin() override;
        virtual void Execute( uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ ) override;
        virtual void Dispatch( uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ ) override;
        virtual void End() override;

        const auto GetCommandBuffer() const
        {
            return m_ActiveComputeCommandBuffer;
        }

        void BindDS( VkDescriptorSet descriptorSet );
        void PushConstant( uint32_t size, void* data );

        void ReadBuffer( uint32_t bufferSize ); // TEMP

        virtual void Invalidate() override;

    private:
        std::shared_ptr<Shader> m_Shader;
        VkPipeline              m_ComputePipeline;
        VkPipelineLayout        m_ComputePipelineLayout;
        VkPipelineCache         m_PipelineCache;

        VkCommandBuffer m_ActiveComputeCommandBuffer = nullptr;
    };
} // namespace Desert::Graphic::API::Vulkan