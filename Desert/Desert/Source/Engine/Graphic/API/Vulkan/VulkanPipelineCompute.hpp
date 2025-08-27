#pragma once

#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanMaterialBackend.hpp>

#include <vulkan/vulkan.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanPipelineCompute final : public PipelineCompute
    {
    public:
        VulkanPipelineCompute( const std::shared_ptr<Shader>& shader );

        virtual void Begin() override;
        virtual void Execute( const std::shared_ptr<Image>& imageForProccess, std::shared_ptr<Image>& outputImage,
                              uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ ) override;
        virtual void ExecuteMipLevel( const std::shared_ptr<Image>& imageForProccess, uint32_t mipLevel,
                                      uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ ) override;
        virtual void Dispatch( uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ ) override;
        virtual void End() override;

        const auto GetCommandBuffer() const
        {
            return m_ActiveComputeCommandBuffer;
        }

        virtual void Invalidate() override;
        virtual void Release() override;

        void BindDescriptorSets( VkDescriptorSet descriptorSet, uint32_t frameIndex );
        void UpdateDescriptorSet( uint32_t frameIndex, const std::vector<VkWriteDescriptorSet>& writes,
                                  VkDescriptorSet descriptorSet, uint32_t setIndex = 0 );

    private:
    private:
        std::weak_ptr<Shader> m_Shader;
        VkPipeline            m_ComputePipeline;
        VkPipelineLayout      m_ComputePipelineLayout;
        VkPipelineCache       m_PipelineCache;

        std::unique_ptr<VulkanMaterialBackend> m_VulkanMaterialBackend;

        VkCommandBuffer m_ActiveComputeCommandBuffer = nullptr;
    };
} // namespace Desert::Graphic::API::Vulkan