#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanSwapChain // maybe should be Singleton?
    {
    public:
        VulkanSwapChain() = default;

        void Init( const VkInstance instance, const std::shared_ptr<VulkanLogicalDevice>& device );

        void                     InitSurface( GLFWwindow* window );
        Common::Result<VkResult> Create( uint32_t width, uint32_t height );

        Common::Result< VkSurfaceFormatKHR> GetImageFormatAndColorSpace();

    private:
        std::shared_ptr<VulkanLogicalDevice> m_LogicalDevice;
        VkInstance                           m_VulkanInstance;

        VkSwapchainKHR m_SwapChain;
        VkSurfaceKHR m_Surface;

        VkFormat        m_ColorFormat;
        VkColorSpaceKHR m_ColorSpace;

        struct SwapChainBuffer
        {
            VkImage     image;
            VkImageView view;
        };
        std::vector<VkImage> m_Images;
    };
} // namespace Desert::Graphic::API::Vulkan