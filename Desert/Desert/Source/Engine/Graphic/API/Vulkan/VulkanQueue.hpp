#pragma once

#include <vulkan/vulkan.h>

namespace Desert::Graphic::API::Vulkan
{
    struct VulkanSwapChain;

    class VulkanQueue final
    {
    public:
        VulkanQueue( VulkanSwapChain* swapChain );

        void                     PrepareFrame();
        void                     Present();
        Common::Result<VkResult> Init();
        void                     Release();

        const auto& GetDrawCommandBuffers() const
        {
            return m_DrawCommandBuffers;
        }

        const auto& GetComputeCommandBuffers() const
        {
            return m_ComputeCommandBuffers;
        }

    private:
        Common::Result<VkResult> QueuePresent( VkQueue queue, uint32_t imageIndex, VkSemaphore waitSemaphore );

    private:
        uint32_t m_ImageIndex = ~0;

        VulkanSwapChain* m_SwapChain;

        struct
        {
            // Swap chain
            VkSemaphore PresentComplete;
            // Command buffer
            VkSemaphore RenderComplete;
        } m_Semaphores;

        std::vector<std::pair<VkCommandBuffer, VkCommandBuffer>>
                                     m_DrawCommandBuffers; // main command buffer | second command buffer
        std::vector<VkCommandBuffer> m_ComputeCommandBuffers;
        std::vector<VkFence>         m_WaitFences;
    };
} // namespace Desert::Graphic::API::Vulkan