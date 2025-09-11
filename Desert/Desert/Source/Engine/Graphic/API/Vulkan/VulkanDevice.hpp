#pragma once

#include <Engine/Core/Device.hpp>

#include <vulkan/vulkan.h>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanPhysicalDevice final
    {
    public:
        VulkanPhysicalDevice();
        ~VulkanPhysicalDevice() = default;

        struct QueueFamilyIndices
        {
            std::optional<int32_t> GraphicsFamily;
            std::optional<int32_t> ComputeFamily;
            std::optional<int32_t> TransferFamily;
            std::optional<int32_t> PresentFamily;
        };

        const VkPhysicalDevice& GetVulkanPhysicalDevice() const
        {
            return m_PhysicalDevice;
        }
        bool IsExtensionSupported( const std::string& extensionName ) const
        {
            return m_SupportedExtensions.find( extensionName ) != m_SupportedExtensions.end();
        }

        std::optional<int32_t> GetGraphicsFamily() const
        {
            return m_QueueFamilyIndices.GraphicsFamily;
        }

        std::optional<int32_t> GetComputeFamily() const
        {
            return m_QueueFamilyIndices.ComputeFamily;
        }

        std::optional<int32_t> GetTransferFamily() const
        {
            return m_QueueFamilyIndices.TransferFamily;
        }

        Common::Result<bool> CreateDevice();

        VkFormat GetDepthFormat() const
        {
            return m_DepthFormat;
        }

        static std::shared_ptr<VulkanPhysicalDevice> Create();

    private:
        VkFormat           FindDepthFormat() const;
        QueueFamilyIndices GetQueueFamilyIndices( int flags );

    private:
        VkPhysicalDevice                     m_PhysicalDevice = VK_NULL_HANDLE;
        std::vector<VkQueueFamilyProperties> m_QueueFamilyProperties;
        std::vector<VkDeviceQueueCreateInfo> m_QueueCreateInfos;
        QueueFamilyIndices                   m_QueueFamilyIndices;

        VkFormat m_DepthFormat = VK_FORMAT_UNDEFINED;

        bool         m_SupportWideLines = false;
        VkDeviceSize m_UniformBufferOffsetAlignment;

        std::unordered_set<std::string> m_SupportedExtensions;

    private:
        friend class VulkanLogicalDevice;
    };

    class VulkanLogicalDevice : public Engine::Device
    {
    public:
        VulkanLogicalDevice();
        ~VulkanLogicalDevice() = default;

        const auto& GetPhysicalDevice() const
        {
            return m_PhysicalDevice;
        }
        const VkDevice GetVulkanLogicalDevice() const
        {
            return m_LogicalDevice;
        }

        VkQueue GetGraphicsQueue()
        {
            return m_GraphicsQueue;
        }
        VkQueue GetComputeQueue()
        {
            return m_ComputeQueue;
        }

        void Destroy();

        Common::Result<bool> CreateDevice();

    private:
        std::shared_ptr<VulkanPhysicalDevice> m_PhysicalDevice;
        VkDevice                              m_LogicalDevice;

        VkQueue m_GraphicsQueue;
        VkQueue m_ComputeQueue;
        VkQueue m_TransferQueue;

        friend class CommandBufferAllocator;
    };
} // namespace Desert::Graphic::API::Vulkan