#pragma once

#include <vulkan/vulkan.h>

#include <Engine/Graphic/RendererContext.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanAllocator.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanAllocator;
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

        [[nodiscard]] Common::Result<VkResult> CreateVKInstance();

        const auto& GetVulkanAllocator() const
        {
            return m_VulkanAllocator;
        }

    private:
        inline static VkInstance         s_VulkanInstance;
        VkDebugReportCallbackEXT         m_DebugReportCallback = VK_NULL_HANDLE;
        std::weak_ptr<Window>            m_Window;
        std::unique_ptr<VulkanAllocator> m_VulkanAllocator;
    };

} // namespace Desert::Graphic::API::Vulkan