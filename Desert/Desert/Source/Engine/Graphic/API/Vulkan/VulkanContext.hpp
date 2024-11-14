#pragma once

#include <vulkan/vulkan.h>

#include <Engine/Graphic/RendererContext.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanSwapChain.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanQueue.hpp>

namespace Desert::Graphic::API::Vulkan
{
 
    class VulkanContext final : public RendererContext, public Common::Singleton<VulkanContext>
    {
    public:
        VulkanContext( GLFWwindow* window );
        virtual ~VulkanContext() = default;

        virtual void BeginFrame() const override;
        virtual void EndFrame() const override;

        virtual void OnResize( uint32_t width, uint32_t height ) override
        {
        }
        virtual void Shutdown() override
        {
        }

        [[nodiscard]] static const VkInstance& GetVulkanInstance()
        {
            return s_VulkanInstance;
        }

        [[nodiscard]] const auto& GetVulkanSwapChain() const
        {
            return m_SwapChain;
        }

        [[nodiscard]] const auto& GetVulkanQueue()const
        {
            return m_VulkanQueue;
        }

        [[nodiscard]] Common::Result<VkResult> CreateVKInstance();

    private:
        inline static VkInstance s_VulkanInstance;
        VkDebugReportCallbackEXT m_DebugReportCallback = VK_NULL_HANDLE;
        GLFWwindow*              m_GLFWwindow;

        std::unique_ptr<VulkanSwapChain> m_SwapChain;
        std::unique_ptr<VulkanQueue>     m_VulkanQueue;
    };

} // namespace Desert::Graphic::API::Vulkan