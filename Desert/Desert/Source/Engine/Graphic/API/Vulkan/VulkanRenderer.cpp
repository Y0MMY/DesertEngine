#include <Engine/Graphic/API/Vulkan/VulkanRenderer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderCommandBuffer.hpp>
#include <Engine/Graphic/Renderer.hpp>

namespace Desert::Graphic::API::Vulkan
{

    void VulkanRendererAPI::BeginFrame()
    {
        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        VkCommandBufferBeginInfo cmdBufferBeginInfo{};
        cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        m_CurrentCommandBuffer = VulkanRenderCommandBuffer::GetInstance().RT_GetCommandBuffer( frameIndex );
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

        VkImageSubresourceRange ImageRange      = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                                    .baseMipLevel   = 0,
                                                    .levelCount     = 1,
                                                    .baseArrayLayer = 0,
                                                    .layerCount     = 1 };
        VkClearColorValue       clearColorValue = { 1.0, 0.0, 0.0, 0.0 };
        vkCmdClearColorImage( m_CurrentCommandBuffer, image, VK_IMAGE_LAYOUT_GENERAL, &clearColorValue, 1,
                              &ImageRange );
    }

} // namespace Desert::Graphic::API::Vulkan