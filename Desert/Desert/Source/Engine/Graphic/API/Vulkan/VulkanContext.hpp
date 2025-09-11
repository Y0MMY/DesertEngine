#pragma once

#include <vulkan/vulkan.h>

#include <Engine/Graphic/RendererContext.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanQueue.hpp>

namespace Desert::Graphic::API::Vulkan
{

    class VulkanContext final : public RendererContext
    {
    public:
        VulkanContext( const std::shared_ptr<Window>& window );
        virtual ~VulkanContext() = default;

        virtual void Init() override;

        virtual void BeginFrame() const override;
        virtual void EndFrame() const override;

        virtual void OnResize( uint32_t width, uint32_t height ) override
        {
        }
        virtual void Shutdown() override;

        [[nodiscard]] static const VkInstance& GetVulkanInstance()
        {
            return s_VulkanInstance;
        }

        [[nodiscard]] const auto& GetVulkanQueue() const
        {
            return m_VulkanQueue;
        }

        [[nodiscard]] Common::Result<VkResult> CreateVKInstance();

    private:
        inline static VkInstance s_VulkanInstance;
        VkDebugReportCallbackEXT m_DebugReportCallback = VK_NULL_HANDLE;
        std::weak_ptr<Window>    m_Window;

        std::unique_ptr<VulkanQueue> m_VulkanQueue;
    };

} // namespace Desert::Graphic::API::Vulkan