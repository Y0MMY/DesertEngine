#pragma once

#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanMaterialBackend.hpp>

#include <vulkan/vulkan.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanPipelineCompute final : public ComputePipeline
    {
    public:
        VulkanPipelineCompute( const ComputePipelineSpecification& spec );
        ~VulkanPipelineCompute() override;

        [[nodiscard]] virtual PipelineType GetType() const override { return PipelineType::Compute; }
        [[nodiscard]] virtual std::shared_ptr<Shader> GetShader() const override { return m_Specification.Shader; }

        [[nodiscard]] virtual const ComputePipelineSpecification& GetSpecification() const override
        {
            return m_Specification;
        }

        virtual void UpdateStorageBuffer( void* data, std::size_t size ) override;

        virtual void Invalidate() override;
        virtual void Release() override;

        const VkPipeline GetVkPipeline() const
        {
            return m_ComputePipeline;
        }

        const VkPipelineLayout GetVkPipelineLayout() const
        {
            return m_ComputePipelineLayout;
        }

        const auto GetCommandBuffer() const
        {
            return m_ActiveComputeCommandBuffer;
        }

        void BindDescriptorSets( VkDescriptorSet descriptorSet, uint32_t frameIndex );
        void UpdateDescriptorSet( uint32_t frameIndex, const std::vector<VkWriteDescriptorSet>& writes,
                                  VkDescriptorSet descriptorSet, uint32_t setIndex = 0 );

        VulkanMaterialBackend* GetVulkanMaterialBackend() const
        {
            return m_VulkanMaterialBackend.get();
        }

    private:
        ComputePipelineSpecification m_Specification;
        VkPipeline                   m_ComputePipeline       = VK_NULL_HANDLE;
        VkPipelineLayout             m_ComputePipelineLayout = VK_NULL_HANDLE;
        VkPipelineCache              m_PipelineCache         = VK_NULL_HANDLE;

        std::unique_ptr<VulkanMaterialBackend> m_VulkanMaterialBackend;

        VkCommandBuffer m_ActiveComputeCommandBuffer = nullptr;
        Common::Memory::Buffer m_StorageBuffer;
    };
} // namespace Desert::Graphic::API::Vulkan