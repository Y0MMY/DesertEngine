#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>

#include <Engine/Core/EngineContext.hpp>

namespace Desert::Graphic::API::Vulkan
{
    namespace
    {
        Common::Result<VkResult> FlushCommandBuffer( VkDevice device, VkCommandPool commandPool,
                                                     VkCommandBuffer commandBuffer, VkQueue queue )
        {
            if ( commandBuffer == VK_NULL_HANDLE )
            {
                return Common::MakeError<VkResult>( "Command buffer is VK_NULL_HANDLE" );
            }

            VK_RETURN_RESULT_IF_FALSE_TYPE( VkResult, vkEndCommandBuffer( commandBuffer ) )

            VkSubmitInfo submitInfo       = {};
            submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers    = &commandBuffer;

            VkFence fence;
            // Create fence to ensure that the command buffer has finished executing
            VkFenceCreateInfo fenceCreateInfo = {};
            fenceCreateInfo.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceCreateInfo.flags             = 0;
            VK_RETURN_RESULT_IF_FALSE( vkCreateFence( device, &fenceCreateInfo, nullptr, &fence ) );
            VK_RETURN_RESULT_IF_FALSE_TYPE( VkResult, vkQueueSubmit( queue, 1, &submitInfo, fence ) );

            // Wait for the fence to signal that command buffer has finished executing
            VK_CHECK_RESULT( vkWaitForFences( device, 1, &fence, VK_TRUE, UINT64_MAX ) );
            vkDestroyFence( device, fence, nullptr );

            return Common::MakeSuccess( VK_SUCCESS );
        }
    } // namespace

    CommandBufferAllocator::CommandBufferAllocator( const VulkanLogicalDevice& device )
    {
        const uint32_t frames = 3U; // TODO

        {
            m_CommandGraphicPool.resize( frames );

            VkCommandPoolCreateInfo cmdPoolInfo = {};
            cmdPoolInfo.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            cmdPoolInfo.queueFamilyIndex        = device.GetPhysicalDevice()->GetGraphicsFamily().value();
            cmdPoolInfo.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            for ( auto& cmdPool : m_CommandGraphicPool )
            {
                VK_CHECK_RESULT(
                     vkCreateCommandPool( device.GetVulkanLogicalDevice(), &cmdPoolInfo, nullptr, &cmdPool ) );
            }
        }

        {
            m_ComputeCommandPool.resize( frames );

            VkCommandPoolCreateInfo cmdPoolInfo = {};
            cmdPoolInfo.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            cmdPoolInfo.queueFamilyIndex        = device.GetPhysicalDevice()->GetComputeFamily().value();
            cmdPoolInfo.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            for ( auto& cmdPool : m_ComputeCommandPool )
            {
                VK_CHECK_RESULT(
                     vkCreateCommandPool( device.GetVulkanLogicalDevice(), &cmdPoolInfo, nullptr, &cmdPool ) );
            }
        }

        m_GraphicsQueue = device.m_GraphicsQueue;
        m_ComputeQueue  = device.m_ComputeQueue;

        m_LogicalDevice = device.m_LogicalDevice;
    }

    Common::Result<VkCommandBuffer> CommandBufferAllocator::RT_GetCommandBufferCompute( bool begin /*= false */ )
    {
        const auto                  frame = EngineContext::GetInstance().GetCurrentFrameIndex();
        VkCommandBufferAllocateInfo allocateInfo;
        allocateInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        allocateInfo.commandPool        = m_ComputeCommandPool[frame];
        allocateInfo.pNext              = VK_NULL_HANDLE;

        VkCommandBuffer cmdBuffer;
        VK_RETURN_RESULT_IF_FALSE_TYPE( VkCommandBuffer,
                                        vkAllocateCommandBuffers( m_LogicalDevice, &allocateInfo, &cmdBuffer ) );

        if ( begin )
        {
            VkCommandBufferBeginInfo cmdBufferBeginInfo{};
            cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            VK_RETURN_RESULT_IF_FALSE_TYPE( VkCommandBuffer,
                                            vkBeginCommandBuffer( cmdBuffer, &cmdBufferBeginInfo ) );
        }

        return Common::MakeSuccess( cmdBuffer );
    }

    Common::Result<VkCommandBuffer>
    CommandBufferAllocator::RT_AllocateCommandBufferGraphic( bool begin /*= false */ )
    {
        const auto frame = EngineContext::GetInstance().GetCurrentFrameIndex();

        VkCommandBufferAllocateInfo allocateInfo;
        allocateInfo.pNext              = VK_NULL_HANDLE;
        allocateInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        allocateInfo.commandPool        = m_CommandGraphicPool[frame];

        VkCommandBuffer cmdBuffer;
        VK_RETURN_RESULT_IF_FALSE_TYPE( VkCommandBuffer,
                                        vkAllocateCommandBuffers( m_LogicalDevice, &allocateInfo, &cmdBuffer ) );

        if ( begin )
        {
            VkCommandBufferBeginInfo cmdBufferBeginInfo{};
            cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            VK_RETURN_RESULT_IF_FALSE_TYPE( VkCommandBuffer,
                                            vkBeginCommandBuffer( cmdBuffer, &cmdBufferBeginInfo ) );
        }

        return Common::MakeSuccess( cmdBuffer );
    }

    Common::Result<VkResult> CommandBufferAllocator::RT_FlushCommandBufferCompute( VkCommandBuffer commandBuffer )
    {
        const auto frame = EngineContext::GetInstance().GetCurrentFrameIndex();

        return FlushCommandBuffer( m_LogicalDevice, m_ComputeCommandPool[frame], commandBuffer, m_ComputeQueue );
    }

    Common::Result<VkResult> CommandBufferAllocator::RT_FlushCommandBufferGraphic( VkCommandBuffer commandBuffer )
    {
        const auto frame = EngineContext::GetInstance().GetCurrentFrameIndex();

        return FlushCommandBuffer( m_LogicalDevice, m_CommandGraphicPool[frame], commandBuffer, m_GraphicsQueue );
    }

    Common::Result<VkCommandBuffer> CommandBufferAllocator::RT_AllocateSecondCommandBufferGraphic()
    {
        const auto frame = EngineContext::GetInstance().GetCurrentFrameIndex();

        VkCommandBufferAllocateInfo allocateInfo = {};
        allocateInfo.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandPool                 = m_CommandGraphicPool[frame];
        allocateInfo.level                       = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        allocateInfo.commandBufferCount          = 1;

        VkCommandBuffer cmdBuffer;
        VK_RETURN_RESULT_IF_FALSE_TYPE( VkCommandBuffer,
                                        vkAllocateCommandBuffers( m_LogicalDevice, &allocateInfo, &cmdBuffer ) );

        return Common::MakeSuccess( cmdBuffer );
    }

} // namespace Desert::Graphic::API::Vulkan