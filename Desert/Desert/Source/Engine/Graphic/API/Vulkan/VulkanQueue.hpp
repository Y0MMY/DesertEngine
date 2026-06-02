#pragma once

#include <vulkan/vulkan.h>
#include <vector>

namespace Desert::Graphic::API::Vulkan
{
    struct VulkanSwapChain;

    class VulkanQueue final
    {
    public:
        VulkanQueue( VulkanSwapChain* swapChain );

        void PrepareFrame();
        void Submit();
        void Present();

        Common::ResultStr<VkResult> Init();
        void                     Release();

        const auto& GetDrawCommandBuffers() const
        {
            return m_DrawCommandBuffers;
        }

        const auto& GetComputeCommandBuffers() const
        {
            return m_ComputeCommandBuffers;
        }

        uint32_t GetImageIndex() const { return m_ImageIndex; }
        VkCommandBuffer GetDrawCommandBuffer() const;

    private:
        Common::ResultStr<VkResult> QueuePresent( VkQueue queue, uint32_t imageIndex, VkSemaphore waitSemaphore );

    private:
        uint32_t m_ImageIndex = ~0;

        VulkanSwapChain* m_SwapChain;

        struct Semaphores
        {
            VkSemaphore PresentComplete;
            VkSemaphore RenderComplete;
        };

        std::vector<Semaphores>      m_FrameSemaphores;
        std::vector<VkCommandBuffer> m_DrawCommandBuffers;
        std::vector<VkCommandBuffer> m_ComputeCommandBuffers;
        std::vector<VkFence>         m_WaitFences;
    };
} // namespace Desert::Graphic::API::Vulkan
