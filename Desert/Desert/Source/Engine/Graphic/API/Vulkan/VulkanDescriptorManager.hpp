#pragma once

#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanDescriptorManager
    {
    public:
        struct DescriptorSetInfo
        {
            VkDescriptorSet       Set;
            VkDescriptorSetLayout Layout;
        };

        VulkanDescriptorManager();
        ~VulkanDescriptorManager();

        Common::Result<DescriptorSetInfo> GetLast( const std::shared_ptr<VulkanShader>& shader,
                                                   uint32_t frameIndex, uint32_t setIndex ) const;

        // Initialize with max frames in flight
        void Initialize( uint32_t framesInFlight );

        // Get or create descriptor sets for a shader
        Common::Result<DescriptorSetInfo> GetDescriptorSet( const std::shared_ptr<VulkanShader>& shader,
                                                            uint32_t frameIndex, uint32_t setIndex = 0 );

        // Update descriptor set with new resources
        void UpdateDescriptorSet( const std::shared_ptr<VulkanShader>& shader, uint32_t frameIndex,
                                  uint32_t setIndex, const std::vector<VkWriteDescriptorSet>& writes );

        // Bind descriptor sets for rendering
        void BindDescriptorSets( VkCommandBuffer cmdBuffer, VkPipelineBindPoint bindPoint, VkPipelineLayout layout,
                                 const std::shared_ptr<VulkanShader>& shader, uint32_t frameIndex,
                                 uint32_t firstSet = 0 );

        // Cleanup resources for a frame
        void CleanupFrame( uint32_t frameIndex );

    private:
        struct FrameData
        {
            VkDescriptorPool                                                                DescriptorPool;
            std::unordered_map<std::shared_ptr<VulkanShader>, std::vector<VkDescriptorSet>> LastDescriptorSets;
        };

        std::vector<FrameData> m_Frames;
        uint32_t               m_FramesInFlight = 0;

        VkDescriptorPool CreateDescriptorPool();
        void             DestroyDescriptorPool( VkDescriptorPool pool );
    };
} // namespace Desert::Graphic::API::Vulkan