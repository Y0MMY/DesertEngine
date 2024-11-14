
#include <Engine/Graphic/API/Vulkan/VulkanQueue.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanHelper.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanSwapChain.hpp>
#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Core/EngineContext.h>

namespace Desert::Graphic::API::Vulkan
{

    namespace
    {
        Common::Result<VkSemaphore> CreateSemaphore( VkDevice device )
        {
            VkSemaphoreCreateInfo createInfo{
                 .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = VK_NULL_HANDLE, .flags = 0 };

            VkSemaphore semaphore;

            VK_RETURN_RESULT_IF_FALSE_TYPE( VkSemaphore,
                                            vkCreateSemaphore( device, &createInfo, VK_NULL_HANDLE, &semaphore ) );

            return Common::MakeSuccess( semaphore );
        }
    } // namespace

    VulkanQueue::VulkanQueue( VulkanSwapChain* swapChain ) : m_SwapChain( swapChain )
    {
    }

    void VulkanQueue::Present() // TODO: result
    {
        uint32_t currentIndex = EngineContext::GetInstance().m_CurrentBufferIndex;

        const auto& device = m_SwapChain->m_LogicalDevice->GetVulkanLogicalDevice();
        const auto& queue  = m_SwapChain->m_LogicalDevice->GetGraphicsQueue();

        VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo         submitInfo    = {};
        submitInfo.sType                   = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pWaitDstStageMask       = &waitStageMask;
        submitInfo.pWaitSemaphores         = &m_Semaphores.PresentComplete;
        submitInfo.waitSemaphoreCount      = 1;
        submitInfo.pSignalSemaphores       = &m_Semaphores.RenderComplete;
        submitInfo.signalSemaphoreCount    = 1;
        submitInfo.pCommandBuffers         = &m_DrawCommandBuffers[currentIndex];
        submitInfo.commandBufferCount      = 1;

        vkResetFences( device, 1, &m_WaitFences[currentIndex] );

        uint32_t imageIndex = ~0U;
        m_SwapChain->AcquireNextImage( m_Semaphores.PresentComplete, &imageIndex );

        // Submit to the graphics queue passing a wait fence
        VK_CHECK_RESULT( vkQueueSubmit( queue, 1, &submitInfo, m_WaitFences[currentIndex] ) );

        const auto& queuePresent = QueuePresent( queue, imageIndex, m_Semaphores.RenderComplete );

        uint32_t newCurrentFrame                          = ( currentIndex + 1 ) % m_SwapChain->GetImageCount();
        EngineContext::GetInstance().m_CurrentBufferIndex = newCurrentFrame;
        vkWaitForFences( device, 1, &m_WaitFences[newCurrentFrame], VK_TRUE,
                         UINT64_MAX );
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

    Common::Result<VkResult> VulkanQueue::Init()
    {
        const auto& device     = m_SwapChain->m_LogicalDevice->GetVulkanLogicalDevice();
        const auto  semaphore1 = CreateSemaphore( device );
        const auto  semaphore2 = CreateSemaphore( device );

        if ( !semaphore1.IsSuccess() )
        {
            return Common::MakeError<VkResult>( semaphore1.GetError() );
        }

        if ( !semaphore2.IsSuccess() )
        {
            return Common::MakeError<VkResult>( semaphore2.GetError() );
        }

        m_Semaphores.PresentComplete = semaphore1.GetValue();
        m_Semaphores.RenderComplete  = semaphore2.GetValue();

        m_DrawCommandBuffers.resize( m_SwapChain->GetImageCount() );

        for ( uint32_t i = 0; i < m_DrawCommandBuffers.size(); i++ )
        {
            const auto& buffer = CommandBufferAllocator::GetInstance().RT_GetCommandBufferGraphic();
            if ( !buffer.IsSuccess() )
            {
                return Common::MakeError<VkResult>( buffer.GetError() );
            }
            m_DrawCommandBuffers[i] = buffer.GetValue();
        }

        m_WaitFences.resize( m_SwapChain->GetImageCount() );
        // Wait fences to sync command buffer access
        VkFenceCreateInfo fenceCreateInfo{};
        fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for ( size_t i = 0; i < m_WaitFences.size(); ++i )
        {

            VK_RETURN_RESULT_IF_FALSE( vkCreateFence( m_SwapChain->m_LogicalDevice->GetVulkanLogicalDevice(),
                                                      &fenceCreateInfo, nullptr, &m_WaitFences[i] ) );
        }

        return Common::MakeSuccess( VK_SUCCESS );
    }
} // namespace Desert::Graphic::API::Vulkan