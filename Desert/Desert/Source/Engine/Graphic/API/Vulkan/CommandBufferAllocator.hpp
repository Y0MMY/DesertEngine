#pragma once

#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class CommandBufferAllocator : public Common::Singleton<CommandBufferAllocator>
    {
    public:
        CommandBufferAllocator( const std::shared_ptr<VulkanLogicalDevice>& device );

        Common::Result<VkCommandBuffer> RT_GetCommandBufferCompute( bool begin = false );
        Common::Result<VkCommandBuffer> RT_AllocateCommandBufferGraphic( bool begin = false );
        Common::Result<VkCommandBuffer> RT_AllocateCommandBufferTransferOps( bool begin = false );

        Common::Result<VkCommandBuffer> RT_AllocateSecondCommandBufferGraphic();

        Common::Result<VkResult> RT_FlushCommandBufferCompute( VkCommandBuffer commandBuffer );
        Common::Result<VkResult> RT_FlushCommandBufferGraphic( VkCommandBuffer commandBuffer );
        Common::Result<VkResult> RT_FlushCommandBufferTransferOps( VkCommandBuffer commandBuffer );

        const auto& GetCommandGraphicPool() const
        {
            return m_CommandGraphicPool;
        }

        const auto& GetCommandComputePool() const
        {
            return m_ComputeCommandPool;
        }

    private:
        std::vector<VkCommandPool> m_CommandGraphicPool;
        std::vector<VkCommandPool> m_ComputeCommandPool;
        std::vector<VkCommandPool> m_TransferOpsCommandPool;

        VkQueue m_GraphicsQueue;
        VkQueue m_ComputeQueue;
        VkQueue m_TransferOpsQueue;

        VkDevice m_LogicalDevice;
    };
} // namespace Desert::Graphic::API::Vulkan