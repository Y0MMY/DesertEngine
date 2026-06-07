#pragma once

#include <Engine/Graphic/Materials/MaterialBackend.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanAllocator.hpp>

#include <vulkan/vulkan.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanMaterialBackend final : public MaterialBackend
    {
    public:
        VulkanMaterialBackend( const std::shared_ptr<Shader>& shader );
        ~VulkanMaterialBackend();

        virtual void InitializeDefaults() override;

        virtual void ApplyUniformBuffer( MaterialProperty* prop ) override;
        virtual void ApplyStorageBuffer( MaterialProperty* prop ) override;
        virtual void ApplyTexture2D( MaterialProperty* prop ) override;
        virtual void ApplyTextureCube( MaterialProperty* prop ) override;

        virtual void FlushUpdates() override;

        virtual void ApplyPushConstants( MaterialExecutor* material, GraphicsPipeline* pipeline ) override;

        VkDescriptorSet GetDescriptorSet( uint32_t frameIndex, uint32_t setIndex = 0 ) const;

        void UpdateDescriptorSets( const std::vector<VkWriteDescriptorSet>& writes, bool force = false );

        void BindDescriptorSets( VkCommandBuffer cmdBuffer, VkPipelineLayout layout, VkPipelineBindPoint bindPoint,
                                 uint32_t frameIndex );

        void ResetFrameUpdateState( uint32_t frameIndex );
        bool HasDescriptorSets() const;

    private:
        void InitializeWithFallbacks();

        void AllocateDescriptorSets();
        void CreateDescriptorPool();

        std::shared_ptr<VulkanShader> m_VulkanShader; // TODO: weak ptr
        VkDescriptorPool              m_DescriptorPool = VK_NULL_HANDLE;

        // [frame][set]
        std::vector<std::vector<VkDescriptorSet>> m_DescriptorSets;

        // Track updates per frame: [frame][set] (Absolute frame count)
        std::vector<std::vector<uint64_t>> m_DescriptorSetsUpdateFrame;

        VkBuffer      m_DummyBuffer = VK_NULL_HANDLE;
        VmaAllocation m_DummyAllocation = nullptr;
    };
} // namespace Desert::Graphic::API::Vulkan