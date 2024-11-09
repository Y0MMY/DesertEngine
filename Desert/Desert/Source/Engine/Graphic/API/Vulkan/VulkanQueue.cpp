
#include <Engine/Graphic/API/Vulkan/VulkanQueue.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanHelper.hpp>

namespace Desert::Graphic::API::Vulkan
{

    VulkanQueue::VulkanQueue( VulkanSwapChain* swapChain ) : m_SwapChain( swapChain )
    {
    }

    void VulkanQueue::Present()
    {
        auto& device = GetLogicalDevice()->GetVulkanLogicalDevice();
        vkWaitForFences(device, 1, &m_WaitFences[m_CurrentBufferIndex], VK_TRUE, UINT64_MAX);

        VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo         submitInfo    = {};
        submitInfo.sType                   = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pWaitDstStageMask       = &waitStageMask;
        submitInfo.pWaitSemaphores         = &m_Semaphores.PresentComplete;
        submitInfo.waitSemaphoreCount      = 1;
        submitInfo.pSignalSemaphores       = &m_Semaphores.RenderComplete;
        submitInfo.signalSemaphoreCount    = 1;
        submitInfo.pCommandBuffers         = &m_DrawCommandBuffers[m_CurrentBufferIndex];
        submitInfo.commandBufferCount      = 1;

        vkResetFences(device, 1, &m_WaitFences[m_CurrentBufferIndex]);
    }

    Common::Result<VkResult> VulkanQueue::QueuePresent( VkQueue queue, uint32_t imageIndex,
                                                        VkSemaphore waitSemaphore )
    {
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType            = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.pNext            = NULL;
        presentInfo.swapchainCount   = 1;
        presentInfo.pSwapchains      = &m_SwapChain->m_SwapChain;
        presentInfo.pImageIndices    = &imageIndex;
        // Check if a wait semaphore has been specified to wait for before presenting the image
        if ( waitSemaphore != VK_NULL_HANDLE )
        {
            presentInfo.pWaitSemaphores    = &waitSemaphore;
            presentInfo.waitSemaphoreCount = 1;
        }
        VK_RETURN_RESULT( vkQueuePresentKHR( queue, &presentInfo ) );
    }

    void VulkanQueue::Init()
    {
        m_DrawCommandBuffers.resize( m_SwapChain->GetImageCount() );

        for ( uint32_t i = 0; i < m_DrawCommandBuffers.size(); i++ )
        {
            const auto& buffer = GetLogicalDevice()->RT_GetCommandBufferGraphic();
            if ( !buffer.IsSuccess() )
            {
                return; // TODO: error
            }
            m_DrawCommandBuffers[i] = buffer.GetValue();
        }
    }

} // namespace Desert::Graphic::API::Vulkan