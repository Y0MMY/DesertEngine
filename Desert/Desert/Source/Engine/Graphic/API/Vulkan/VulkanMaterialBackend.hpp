#pragma once

#include <Engine/Graphic/Materials/MaterialBackend.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>

#include <vulkan/vulkan.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanMaterialBackend final : public MaterialBackend
    {
    public:
        VulkanMaterialBackend( const std::shared_ptr<Shader>& shader );
        ~VulkanMaterialBackend();

        virtual void ApplyUniformBuffer( MaterialProperty* prop ) override;
        virtual void ApplyTexture2D( MaterialProperty* prop ) override;
        virtual void ApplyTextureCube( MaterialProperty* prop ) override;

        virtual void FlushUpdates() override;

        virtual void ApplyPushConstants( MaterialExecutor* material, Pipeline* pipeline ) override;

        VkDescriptorSet GetDescriptorSet( uint32_t frameIndex, uint32_t setIndex = 0 ) const;

        void BindDescriptorSets( VkCommandBuffer cmdBuffer, VkPipelineLayout layout, VkPipelineBindPoint bindPoint,
                                 uint32_t frameIndex );

    private:
        bool HasDescriptorSets() const;

        void AllocateDescriptorSets();
        void CreateDescriptorPool();

        std::shared_ptr<VulkanShader> m_VulkanShader; // TODO: weak ptr
        VkDescriptorPool              m_DescriptorPool = VK_NULL_HANDLE;

        // [frame][set]
        std::vector<std::vector<VkDescriptorSet>> m_DescriptorSets;
        std::vector<VkWriteDescriptorSet>         m_PendingDescriptorWrites;
    };
} // namespace Desert::Graphic::API::Vulkan