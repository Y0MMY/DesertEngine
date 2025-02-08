#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>

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
            vkFreeCommandBuffers( device, commandPool, 1, &commandBuffer ); // TODO: vkResetCommandBuffer
            return Common::MakeSuccess( VK_SUCCESS );
        }
    } // namespace

    CommandBufferAllocator::CommandBufferAllocator( const VulkanLogicalDevice& device )
    {
        m_CommandGraphicPool = device.m_CommandGraphicPool;
        m_ComputeCommandPool = device.m_ComputeCommandPool;

        m_GraphicsQueue = device.m_GraphicsQueue;
        m_ComputeQueue  = device.m_ComputeQueue;

        m_LogicalDevice = device.m_LogicalDevice;
    }

    Common::Result<VkCommandBuffer> CommandBufferAllocator::RT_GetCommandBufferCompute( bool begin /*= false */ )
    {
        VkCommandBufferAllocateInfo allocateInfo;
        allocateInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        allocateInfo.commandPool        = m_ComputeCommandPool;
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

    Common::Result<VkCommandBuffer> CommandBufferAllocator::RT_GetCommandBufferGraphic( bool begin /*= false */ )
    {
        VkCommandBufferAllocateInfo allocateInfo;
        allocateInfo.pNext              = VK_NULL_HANDLE;
        allocateInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        allocateInfo.commandPool        = m_CommandGraphicPool;

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
        return FlushCommandBuffer( m_LogicalDevice, m_ComputeCommandPool, commandBuffer, m_ComputeQueue );
    }

    Common::Result<VkResult> CommandBufferAllocator::RT_FlushCommandBufferGraphic( VkCommandBuffer commandBuffer )
    {
        return FlushCommandBuffer( m_LogicalDevice, m_CommandGraphicPool, commandBuffer, m_GraphicsQueue );
    }

} // namespace Desert::Graphic::API::Vulkan