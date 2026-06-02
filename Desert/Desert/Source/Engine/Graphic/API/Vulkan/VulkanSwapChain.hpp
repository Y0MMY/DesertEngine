#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanQueue.hpp>

#include <Engine/Graphic/SwapChain.hpp>
#include <Engine/Graphic/Framebuffer.hpp>

#include <memory>
#include <vector>
#include <array>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanSwapChain final : public SwapChain
    {
    public:
        VulkanSwapChain( const GLFWwindow* window );
        ~VulkanSwapChain() override;

        void Init( const VkInstance instance, const std::shared_ptr<Engine::Device>& device );

        Common::ResultStr<bool> CreateSwapChain( const std::shared_ptr<Engine::Device>& device, uint32_t* width,
                                              uint32_t* height ) override;

        Common::ResultStr<bool> GetImageFormatAndColorSpace( const std::shared_ptr<VulkanLogicalDevice>& device );

        uint32_t GetBackBufferCount() const override
        {
            return (uint32_t)m_SwapChainImages.Images.size();
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

        uint32_t GetWidth() const override
        {
            return m_Width;
        }
        uint32_t GetHeight() const override
        {
            return m_Height;
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

        const auto& GetVulkanQueue() const
        {
            return m_VulkanQueue;
        }

        void OnResize( uint32_t width, uint32_t height ) override;

        void Release() override;

        uint32_t GetCurrentBufferIndex() const;
        void     PrepareFrame();
        void     Present();

        [[nodiscard]] std::shared_ptr<::Desert::Graphic::Framebuffer> GetCompositeFramebuffer() const
        {
            return m_CompositeFramebuffer;
        }

    private:
        void InitSurface( GLFWwindow* window, const VkInstance instance );

    private:
        Common::ResultStr<VkResult> AcquireNextImage( VkSemaphore presentCompleteSemaphore, uint32_t* imageIndex );
        Common::ResultStr<VkResult> CreateSwapChainRenderPass();
        Common::ResultStr<VkResult> CreateSwapChainFramebuffers();
        Common::ResultStr<VkResult>
        CreateColorAndDepthImages( const std::shared_ptr<VulkanLogicalDevice>& device );

    private:
        std::unique_ptr<VulkanQueue>       m_VulkanQueue;
        VkSampleCountFlagBits              m_MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        std::weak_ptr<VulkanLogicalDevice> m_LogicalDevice;

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

        std::array<const void*, 2> m_VmaAllocation;

        std::shared_ptr<::Desert::Graphic::Framebuffer> m_CompositeFramebuffer;

    private:
        friend class VulkanQueue;
    };
} // namespace Desert::Graphic::API::Vulkan
