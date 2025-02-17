#pragma once

#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class CommandBufferAllocator : public Common::Singleton<CommandBufferAllocator>
    {
    public:
        CommandBufferAllocator( const VulkanLogicalDevice& device );

        Common::Result<VkCommandBuffer> RT_GetCommandBufferCompute( bool begin = false );
        Common::Result<VkCommandBuffer> RT_GetCommandBufferGraphic( bool begin = false );

        Common::Result<VkResult> RT_FlushCommandBufferCompute( VkCommandBuffer commandBuffer );
        Common::Result<VkResult> RT_FlushCommandBufferGraphic( VkCommandBuffer commandBuffer );

    private:
        VkCommandPool m_CommandGraphicPool = nullptr, m_ComputeCommandPool = nullptr;

        VkQueue m_GraphicsQueue;
        VkQueue m_ComputeQueue;

        VkDevice m_LogicalDevice;
    };
} // namespace Desert::Graphic::API::Vulkan