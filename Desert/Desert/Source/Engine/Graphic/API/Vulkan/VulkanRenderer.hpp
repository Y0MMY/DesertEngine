#pragma once

#include <Engine/Graphic/RendererAPI.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanSwapChain.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanRendererAPI : public RendererAPI
    {
    public:
        virtual void Init() override
        {
        }

        static void ClearImage_Vulkan_test(VkCommandBuffer cmdBuffer, VkImage image)
        {
            static VkClearColorValue ClearColor = { 1.0f, 0.0f, 0.0f, 0.0f };

            VkCommandBufferBeginInfo cmdBufferBeginInfo{};
            cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

            vkBeginCommandBuffer( cmdBuffer, &cmdBufferBeginInfo );

            VkImageSubresourceRange ImageRange = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                                   .baseMipLevel   = 0,
                                                   .levelCount     = 1,
                                                   .baseArrayLayer = 0,
                                                   .layerCount     = 1 };

            vkCmdClearColorImage(cmdBuffer, image, VK_IMAGE_LAYOUT_GENERAL, &ClearColor, 1, &ImageRange);

            VkResult res = vkEndCommandBuffer(cmdBuffer);
        }

    private:
    };
} // namespace Desert::Graphic::API::Vulkan