#pragma once

#include <vulkan/vulkan.h>

#include <Engine/Graphic/API/Vulkan/VulkanSwapChain.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanQueue final
    {
    public:
        VulkanQueue( VulkanSwapChain* swapChain );

        void Present();
        void Init();

    private:
        Common::Result<VkResult> QueuePresent( VkQueue queue, uint32_t imageIndex, VkSemaphore waitSemaphore );

        const auto& GetLogicalDevice() const
        {
            return m_SwapChain->m_LogicalDevice;
        }

    private:
        VulkanSwapChain* m_SwapChain;

        struct
        {
            // Swap chain
            VkSemaphore PresentComplete;
            // Command buffer
            VkSemaphore RenderComplete;
        } m_Semaphores;

        std::vector<VkCommandBuffer> m_DrawCommandBuffers;
        VkCommandPool                m_CommandPool = nullptr; // TODO: should i use pool allocated in device class?

        uint32_t             m_CurrentBufferIndex = 0;
        std::vector<VkFence> m_WaitFences;
    };
} // namespace Desert::Graphic::API::Vulkan