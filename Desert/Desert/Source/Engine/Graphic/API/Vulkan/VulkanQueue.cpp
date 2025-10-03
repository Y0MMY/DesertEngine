
#include <Engine/Graphic/API/Vulkan/VulkanQueue.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>

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
        uint32_t currentIndex = EngineContext::GetInstance().m_CurrentFrameIndex;

        VkDevice device = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetMainDevice() )
                               ->GetVulkanLogicalDevice();
        const auto& queue =
             SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetMainDevice() )->GetGraphicsQueue();

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

        const auto acquire = m_SwapChain->AcquireNextImage( m_Semaphores.PresentComplete, &m_ImageIndex );
        if ( !acquire )
        {
            LOG_ERROR( "[AcquireNextImage] Error: {}", acquire.GetError() );
        }

        // Submit to the graphics queue passing a wait fence
        VK_CHECK_RESULT( vkQueueSubmit( queue, 1, &submitInfo, m_WaitFences[currentIndex] ) );
    }

    void VulkanQueue::Present() // TODO: result
    {
        uint32_t currentIndex = EngineContext::GetInstance().m_CurrentFrameIndex;

        VkDevice device = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetMainDevice() )
                               ->GetVulkanLogicalDevice();
        const auto& queue =
             SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetMainDevice() )->GetGraphicsQueue();

        const auto& queuePresent = QueuePresent( queue, m_ImageIndex, m_Semaphores.RenderComplete );
        if ( !queuePresent.IsSuccess() )
        {
            LOG_INFO( "[QueuePresent] Error: {}", queuePresent.GetError() );
        }

        uint32_t newCurrentFrame = ( currentIndex + 1 ) % m_SwapChain->GetBackBufferCount();
        EngineContext::GetInstance().m_CurrentFrameIndex = newCurrentFrame;
        vkWaitForFences( device, 1, &m_WaitFences[newCurrentFrame], VK_TRUE, UINT64_MAX );
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
        // Check if a wait semaphore has been specified to wait for before presenting the image
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

        return Common::MakeFormattedError<VkResult>( "result: {}", VkResultToString( res ) );
    }

    Common::ResultStr<VkResult> VulkanQueue::Init()
    {
        VkDevice device = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetMainDevice() )
                               ->GetVulkanLogicalDevice();
        const auto semaphore1 = CreateSemaphore( device );
        const auto semaphore2 = CreateSemaphore( device );

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

        m_DrawCommandBuffers.resize( m_SwapChain->GetBackBufferCount() );
        m_ComputeCommandBuffers.resize( m_SwapChain->GetBackBufferCount() );

        for ( uint32_t i = 0; i < m_SwapChain->GetBackBufferCount(); i++ )
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

        m_WaitFences.resize( m_SwapChain->GetBackBufferCount() );
        // Wait fences to sync command buffer access
        VkFenceCreateInfo fenceCreateInfo{};
        fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for ( size_t i = 0; i < m_WaitFences.size(); ++i )
        {

            VK_RETURN_RESULT_IF_FALSE(
                 vkCreateFence( SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetMainDevice() )
                                     ->GetVulkanLogicalDevice(),
                                &fenceCreateInfo, nullptr, &m_WaitFences[i] ) );
        }

        return Common::MakeSuccess( VK_SUCCESS );
    }

    void VulkanQueue::Release()
    {

        VkDevice device = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetMainDevice() )
                               ->GetVulkanLogicalDevice();

        if ( m_Semaphores.PresentComplete != VK_NULL_HANDLE )
        {
            vkDestroySemaphore( device, m_Semaphores.PresentComplete, nullptr );
            m_Semaphores.PresentComplete = VK_NULL_HANDLE;
        }

        if ( m_Semaphores.RenderComplete != VK_NULL_HANDLE )
        {
            vkDestroySemaphore( device, m_Semaphores.RenderComplete, nullptr );
            m_Semaphores.RenderComplete = VK_NULL_HANDLE;
        }

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