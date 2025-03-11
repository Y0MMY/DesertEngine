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

        uint32_t GetImageCount() const
        {
            return m_SwapChainImages.Images.size();
        }

        const auto& GetSwapChainVKImage() const
        {
            return m_SwapChainImages.Images;
        }

        const auto& GetSwapChainVKImagesView() const
        {
            return m_SwapChainImages.ImagesView;
        }

        VkFormat GetColorFormat() const
        {
            return m_ColorFormat;
        }

        VkColorSpaceKHR GetColorSpace() const
        {
            return m_ColorSpace;
        }

        const auto& GetColorImages() const
        {
            return m_ColorImages;
        }

        const auto& GetDepthImages() const
        {
            return m_DepthStencilImages;
        }

        const auto GetRenderPass() const
        {
            return m_VkRenderPass;
        }

        const auto GetVKFramebuffers() const
        {
            return m_SwapChainFramebuffers;
        }

        void OnResize( uint32_t width, uint32_t height );

        void Release();

    private:
        void InitSurface( GLFWwindow* window );

    private:
        Common::Result<VkResult> AcquireNextImage( VkSemaphore presentCompleteSemaphore, uint32_t* imageIndex );
        Common::Result<VkResult> CreateSwapChainRenderPass();
        Common::Result<VkResult> CreateSwapChainFramebuffers();
        Common::Result<VkResult> CreateColorAndDepthImages();

    private:
        VkSampleCountFlagBits m_MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        VulkanLogicalDevice*  m_LogicalDevice;
        VkInstance            m_VulkanInstance;

        VkSwapchainKHR m_SwapChain = VK_NULL_HANDLE;
        VkSurfaceKHR   m_Surface   = VK_NULL_HANDLE;

        VkFormat        m_ColorFormat;
        VkColorSpaceKHR m_ColorSpace;

        uint32_t m_Width  = 0u;
        uint32_t m_Height = 0u;

        struct
        {
            VkImage     Image;
            VkImageView ImageView;
        } m_ColorImages;

        struct
        {
            VkImage     Image;
            VkImageView ImageView;
        } m_DepthStencilImages;

        struct
        {
            std::vector<VkImage>     Images;
            std::vector<VkImageView> ImagesView;
        } m_SwapChainImages;

        std::vector<VkFramebuffer> m_SwapChainFramebuffers;

        VkRenderPass m_VkRenderPass = VK_NULL_HANDLE;

    private:
        friend class VulkanQueue;
    };
} // namespace Desert::Graphic::API::Vulkan