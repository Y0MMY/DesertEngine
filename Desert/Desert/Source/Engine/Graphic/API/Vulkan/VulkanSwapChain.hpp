#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanQueue.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanSwapChain // maybe should be Singleton?
    {
    public:
        VulkanSwapChain() = default;

        void Init( GLFWwindow* window, const VkInstance instance, VulkanLogicalDevice& device );

        Common::Result<VkResult> Create( uint32_t* width, uint32_t* height );

        Common::Result<VkSurfaceFormatKHR> GetImageFormatAndColorSpace();

        uint32_t GetImageCount() const { return m_Images.size(); }

    private:
        void InitSurface( GLFWwindow* window );

    private:
        Common::Result<VkResult> AcquireNextImage( VkSemaphore presentCompleteSemaphore, uint32_t* imageIndex );

    private:
        VulkanLogicalDevice* m_LogicalDevice;
        VkInstance           m_VulkanInstance;

        VulkanQueue m_VulkanQueue;

        VkSwapchainKHR m_SwapChain = VK_NULL_HANDLE;
        VkSurfaceKHR   m_Surface;

        VkFormat        m_ColorFormat;
        VkColorSpaceKHR m_ColorSpace;

        std::vector<VkImage>     m_Images;
        std::vector<VkImageView> m_ImagesView;
    private:

        friend class VulkanQueue;
    };
} // namespace Desert::Graphic::API::Vulkan