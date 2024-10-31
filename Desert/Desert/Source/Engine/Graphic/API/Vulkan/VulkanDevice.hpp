#pragma once

#include <Common/Core/Singleton.hpp>

#include <vulkan/vulkan.h>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanPhysicalDevice
    {
    public:
        VulkanPhysicalDevice() = default;

        struct QueueFamilyIndices
        {
            std::optional<int32_t> GraphicsFamily;
            std::optional<int32_t> ComputeFamily;
        };

        const VkPhysicalDevice& GetPhysicalDevice() const
        {
            return m_PhysicalDevice;
        }
        bool IsExtensionSupported( const std::string& extensionName ) const
        {
            return m_SupportedExtensions.find( extensionName ) != m_SupportedExtensions.end();
        }

        Common::Result<bool> CreateDevice();

        static std::shared_ptr<VulkanPhysicalDevice> Create();

    private:
        QueueFamilyIndices GetQueueFamilyIndices( int flags );

    private:
        VkPhysicalDevice                     m_PhysicalDevice = VK_NULL_HANDLE;
        std::vector<VkQueueFamilyProperties> m_QueueFamilyProperties;
        std::vector<VkDeviceQueueCreateInfo> m_QueueCreateInfos;
        QueueFamilyIndices                   m_QueueFamilyIndices;

        std::unordered_set<std::string> m_SupportedExtensions;

    private:
        friend class VulkanLogicalDevice;
    };

    class VulkanLogicalDevice : public Common::Singleton<VulkanLogicalDevice>
    {
    public:
        VulkanLogicalDevice( const std::shared_ptr<VulkanPhysicalDevice>& physicalDevice );

        const auto& GetPhysicalDevice() const
        {
            return m_PhysicalDevice;
        }
        const VkDevice& GetLogicalDevice() const
        {
            return m_LogicalDevice;
        }

        Common::Result<VkCommandBuffer> RT_GetCommandBufferCompute();
        Common::Result<VkCommandBuffer> RT_GetCommandBufferGraphic();

        Common::Result<VkResult> RT_FlushCommandBufferCompute( VkCommandBuffer commandBuffer );
        Common::Result<VkResult> RT_FlushCommandBufferGraphic( VkCommandBuffer commandBuffer );

        void Destroy();

        Common::Result<bool> CreateDevice();

    private:
        std::shared_ptr<VulkanPhysicalDevice> m_PhysicalDevice;
        VkDevice                              m_LogicalDevice;

        VkCommandPool m_CommandGraphicPool = nullptr, m_ComputeCommandPool = nullptr;

        VkQueue m_GraphicsQueue;
        VkQueue m_ComputeQueue;
    };
} // namespace Desert::Graphic::API::Vulkan