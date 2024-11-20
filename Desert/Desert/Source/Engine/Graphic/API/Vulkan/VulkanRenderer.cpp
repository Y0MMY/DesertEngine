#include <Engine/Graphic/API/Vulkan/VulkanRenderer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderCommandBuffer.hpp>
#include <Engine/Graphic/Renderer.hpp>

namespace Desert::Graphic::API::Vulkan
{
    namespace
    {
        void InsertImageMemoryBarrier( VkCommandBuffer cmdbuffer, VkImage image, VkAccessFlags srcAccessMask,
                                       VkAccessFlags dstAccessMask, VkImageLayout oldImageLayout,
                                       VkImageLayout newImageLayout, VkPipelineStageFlags srcStageMask,
                                       VkPipelineStageFlags dstStageMask )
        {
            VkImageSubresourceRange subresourceRange = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                                         .baseMipLevel   = 0,
                                                         .levelCount     = 1,
                                                         .baseArrayLayer = 0,
                                                         .layerCount     = 1 };

            VkImageMemoryBarrier imageMemoryBarrier{};
            imageMemoryBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

            imageMemoryBarrier.srcAccessMask    = srcAccessMask;
            imageMemoryBarrier.dstAccessMask    = dstAccessMask;
            imageMemoryBarrier.oldLayout        = oldImageLayout;
            imageMemoryBarrier.newLayout        = newImageLayout;
            imageMemoryBarrier.image            = image;
            imageMemoryBarrier.subresourceRange = subresourceRange;

            vkCmdPipelineBarrier( cmdbuffer, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, 1,
                                  &imageMemoryBarrier );
        }
    } // namespace

    void VulkanRendererAPI::BeginFrame()
    {
        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        VkCommandBufferBeginInfo cmdBufferBeginInfo{};
        cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        m_CurrentCommandBuffer = VulkanRenderCommandBuffer::GetInstance().GetCommandBuffer( frameIndex );
        vkBeginCommandBuffer( m_CurrentCommandBuffer, &cmdBufferBeginInfo );
    }

    void VulkanRendererAPI::EndFrame()
    {
        VkResult res = vkEndCommandBuffer( m_CurrentCommandBuffer );

        m_CurrentCommandBuffer = nullptr;
    }

    void VulkanRendererAPI::ClearImage()
    {
        uint32_t    frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();
        const auto& image      = std::static_pointer_cast<Graphic::API::Vulkan::VulkanContext>(
                                 Renderer::GetInstance().GetRendererContext() )
                                 ->GetVulkanSwapChain()
                                 ->GetVKImage()[frameIndex]; // cringe

        VkImageSubresourceRange ImageRange = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                               .baseMipLevel   = 0,
                                               .levelCount     = 1,
                                               .baseArrayLayer = 0,
                                               .layerCount     = 1 };

        InsertImageMemoryBarrier( m_CurrentCommandBuffer, image, VK_ACCESS_MEMORY_READ_BIT,
                                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT );

        VkClearColorValue clearColorValue = { 1.0, 0.0, 0.0, 0.0 };
        vkCmdClearColorImage( m_CurrentCommandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColorValue, 1,
                              &ImageRange );

        InsertImageMemoryBarrier( m_CurrentCommandBuffer, image, VK_ACCESS_TRANSFER_WRITE_BIT,
                                  VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT );
    }

} // namespace Desert::Graphic::API::Vulkan