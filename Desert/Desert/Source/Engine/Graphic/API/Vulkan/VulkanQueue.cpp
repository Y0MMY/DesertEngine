#include <Engine/Graphic/API/Vulkan/VulkanQueue.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanSwapChain.hpp>
#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Core/EngineContext.hpp>

namespace Desert::Graphic::API::Vulkan
{
    namespace
    {
        Common::ResultStr<VkSemaphore> CreateSemaphore( VkDevice device )
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

    void VulkanQueue::PrepareFrame()
    {
        uint32_t currentIndex = EngineContext::GetInstance().GetCurrentFrameIndex();

        VkDevice device = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetDevice() )
                               ->GetVulkanLogicalDevice();

        vkResetFences( device, 1, &m_WaitFences[currentIndex] );

        const auto acquire = m_SwapChain->AcquireNextImage( m_FrameSemaphores[currentIndex].PresentComplete, &m_ImageIndex );
        if ( !acquire )
        {
            // Most commonly VK_ERROR_OUT_OF_DATE_KHR after a window resize — recreate the swapchain (it
            // re-queries the surface extent) and re-acquire from the fresh swapchain.
            m_SwapChain->OnResize( m_SwapChain->GetWidth(), m_SwapChain->GetHeight() );
            const auto reacquire =
                 m_SwapChain->AcquireNextImage( m_FrameSemaphores[currentIndex].PresentComplete, &m_ImageIndex );
            if ( !reacquire )
                LOG_ERROR( "[AcquireNextImage] Error after swapchain recreate: {}", reacquire.GetError() );
        }
    }

    void VulkanQueue::Submit()
    {
        uint32_t currentIndex = EngineContext::GetInstance().GetCurrentFrameIndex();

        const auto& queue =
             SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetDevice() )->GetGraphicsQueue();

        VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo         submitInfo    = {};
        submitInfo.sType                   = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pWaitDstStageMask       = &waitStageMask;
        submitInfo.pWaitSemaphores         = &m_FrameSemaphores[currentIndex].PresentComplete;
        submitInfo.waitSemaphoreCount      = 1;
        submitInfo.pSignalSemaphores       = &m_FrameSemaphores[currentIndex].RenderComplete;
        submitInfo.signalSemaphoreCount    = 1;
        submitInfo.pCommandBuffers         = &m_DrawCommandBuffers[currentIndex];
        submitInfo.commandBufferCount      = 1;

        VK_CHECK_RESULT( vkQueueSubmit( queue, 1, &submitInfo, m_WaitFences[currentIndex] ) );
    }

    void VulkanQueue::Present()
    {
        uint32_t currentIndex = EngineContext::GetInstance().GetCurrentFrameIndex();
        VkDevice device = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetDevice() )
                               ->GetVulkanLogicalDevice();
        const auto& queue =
             SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetDevice() )->GetGraphicsQueue();

        const auto& queuePresent = QueuePresent( queue, m_ImageIndex, m_FrameSemaphores[currentIndex].RenderComplete );
        if ( !queuePresent.IsSuccess() )
        {
            LOG_INFO( "[QueuePresent] Error: {}", queuePresent.GetError() );
        }

        Engine::FrameManager::GetInstance().NextFrame();
        uint32_t newCurrentFrame = Engine::FrameManager::GetInstance().GetCurrentFrameIndex();
        vkWaitForFences( device, 1, &m_WaitFences[newCurrentFrame], VK_TRUE, UINT64_MAX );

        SP_CAST( VulkanContext, EngineContext::GetInstance().GetRendererContext() )
             ->GetVulkanAllocator()
             ->ProcessDeletionQueue();
    }

    VkCommandBuffer VulkanQueue::GetDrawCommandBuffer() const
    {
        return m_DrawCommandBuffers[EngineContext::GetInstance().GetCurrentFrameIndex()];
    }

    Common::ResultStr<VkResult> VulkanQueue::QueuePresent( VkQueue queue, uint32_t imageIndex,
                                                        VkSemaphore waitSemaphore )
    {
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType            = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.pNext            = NULL;
        presentInfo.swapchainCount   = 1;
        presentInfo.pSwapchains      = &m_SwapChain->m_SwapChain;
        presentInfo.pImageIndices    = &imageIndex;
        if ( waitSemaphore != VK_NULL_HANDLE )
        {
            presentInfo.pWaitSemaphores    = &waitSemaphore;
            presentInfo.waitSemaphoreCount = 1;
        }
        auto res = ( vkQueuePresentKHR( queue, &presentInfo ) );
        if ( res == VK_SUCCESS )
        {
            return Common::MakeSuccess( VK_SUCCESS );
        }

        // Window was resized/minimized between acquire and present — recreate the swapchain (it re-queries
        // the current surface extent) and treat this frame as handled. Standard Vulkan resize handling.
        if ( res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR )
        {
            m_SwapChain->OnResize( m_SwapChain->GetWidth(), m_SwapChain->GetHeight() );
            return Common::MakeSuccess( VK_SUCCESS );
        }

        return Common::MakeFormattedError<VkResult>( "result: {}", VkResultToString( res ) );
    }

    Common::ResultStr<VkResult> VulkanQueue::Init()
    {
        VkDevice device = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetDevice() )
                               ->GetVulkanLogicalDevice();

        uint32_t backBufferCount = m_SwapChain->GetBackBufferCount();

        m_FrameSemaphores.resize( backBufferCount );
        for ( uint32_t i = 0; i < backBufferCount; i++ )
        {
            m_FrameSemaphores[i].PresentComplete = CreateSemaphore( device ).GetValue();
            m_FrameSemaphores[i].RenderComplete  = CreateSemaphore( device ).GetValue();
        }

        m_DrawCommandBuffers.resize( backBufferCount );
        m_ComputeCommandBuffers.resize( backBufferCount );

        for ( uint32_t i = 0; i < backBufferCount; i++ )
        {
            // graphic
            {
                const auto&                 gPool = CommandBufferAllocator::GetInstance().GetCommandGraphicPool();
                VkCommandBufferAllocateInfo allocateInfo;
                allocateInfo.pNext              = VK_NULL_HANDLE;
                allocateInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                allocateInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                allocateInfo.commandBufferCount = 1;
                allocateInfo.commandPool        = gPool[i];

                VK_RETURN_RESULT_IF_FALSE_TYPE(
                     VkResult, vkAllocateCommandBuffers( device, &allocateInfo, &m_DrawCommandBuffers[i] ) );
            }

            // compute
            {
                const auto& cPool = CommandBufferAllocator::GetInstance().GetCommandComputePool();

                VkCommandBufferAllocateInfo allocateInfo;
                allocateInfo.pNext              = VK_NULL_HANDLE;
                allocateInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                allocateInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                allocateInfo.commandBufferCount = 1;
                allocateInfo.commandPool        = cPool[i];

                VK_RETURN_RESULT_IF_FALSE_TYPE(
                     VkResult, vkAllocateCommandBuffers( device, &allocateInfo, &m_ComputeCommandBuffers[i] ) );
            }
        }

        m_WaitFences.resize( backBufferCount );

        VkFenceCreateInfo fenceCreateInfo{};
        fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for ( size_t i = 0; i < m_WaitFences.size(); ++i )
        {

            VK_RETURN_RESULT_IF_FALSE(
                 vkCreateFence( device, &fenceCreateInfo, nullptr, &m_WaitFences[i] ) );
        }

        return Common::MakeSuccess( VK_SUCCESS );
    }

    void VulkanQueue::Release()
    {
        VkDevice device = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetDevice() )
                               ->GetVulkanLogicalDevice();

        for ( auto& sem : m_FrameSemaphores )
        {
            if ( sem.PresentComplete != VK_NULL_HANDLE ) vkDestroySemaphore( device, sem.PresentComplete, nullptr );
            if ( sem.RenderComplete != VK_NULL_HANDLE ) vkDestroySemaphore( device, sem.RenderComplete, nullptr );
        }
        m_FrameSemaphores.clear();

        for ( auto& fence : m_WaitFences )
        {
            if ( fence != VK_NULL_HANDLE )
            {
                vkDestroyFence( device, fence, nullptr );
                fence = VK_NULL_HANDLE;
            }
        }
    }
} // namespace Desert::Graphic::API::Vulkan
