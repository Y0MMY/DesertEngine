#pragma once

#include <vulkan/vulkan.h>

#include <Engine/Graphic/RendererContext.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanSwapChain.hpp>

namespace Desert::Graphic::API::Vulkan
{

    class VulkanContext final : public RendererContext, public Common::Singleton<VulkanContext>
    {
    public:
        VulkanContext( GLFWwindow* window );
        virtual ~VulkanContext() = default;

        virtual void BeginFrame() const override
        {
        }
        virtual void EndFrame() const override
        {
        }

        virtual void OnResize( uint32_t width, uint32_t height ) override
        {
        }
        virtual void Shutdown() override
        {
        }

        static const VkInstance& GetVulkanInstance()
        {
            return s_VulkanInstance;
        }

        [[nodiscard]] Common::Result<VkResult> CreateVKInstance();

    private:
        inline static VkInstance s_VulkanInstance;
        VkDebugReportCallbackEXT m_DebugReportCallback = VK_NULL_HANDLE;
        GLFWwindow* m_GLFWwindow;

        std::unique_ptr<VulkanSwapChain> m_SwapChain;
    };

} // namespace Desert::Graphic::API::Vulkan