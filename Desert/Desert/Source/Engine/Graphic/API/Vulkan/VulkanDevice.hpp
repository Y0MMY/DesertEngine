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

        Common::ResultStr<bool> CreateDevice();

        VkFormat GetDepthFormat() const
        {
            return m_DepthFormat;
        }

        const auto& GetCapabilities() const
        {
            return m_Capabilities;
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

        Engine::DeviceCapabilities m_Capabilities;

        std::unordered_set<std::string> m_SupportedExtensions;

    private:
        friend class VulkanLogicalDevice;
    };

    class VulkanLogicalDevice : public Engine::Device
    {
    public:
        VulkanLogicalDevice();
        ~VulkanLogicalDevice() override;

        // Device interface implementation
        [[nodiscard]] const Engine::DeviceCapabilities& GetCapabilities() const override;
        virtual void                                    WaitIdle() const override;
        [[nodiscard]] virtual std::string               GetName() const override;
        [[nodiscard]] bool IsFormatSupported( ::Desert::Core::Formats::ImageFormat format,
                                              Engine::FormatUsage                  usage ) const override;

        const auto& GetPhysicalDevice() const
        {
            return m_PhysicalDevice;
        }
        const VkDevice GetVulkanLogicalDevice() const
        {
            return m_LogicalDevice;
        }

        // ONE device-wide pipeline cache, seeded from Cooked/PipelineCache.bin on create and written
        // back on Destroy — so the driver reuses previously-built pipeline binaries across runs instead
        // of rebuilding every graphics/compute pipeline from scratch each startup. Passed to every
        // vkCreate*Pipelines call (graphics + compute).
        VkPipelineCache GetPipelineCache() const
        {
            return m_PipelineCache;
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

        Common::ResultStr<bool> CreateDevice();

    private:
        // Create the device-wide VkPipelineCache, seeding it from the on-disk cache if present. The driver
        // validates the header (vendor/device/UUID) and silently ignores mismatched or corrupt data.
        void CreatePipelineCache();
        // Serialize the current pipeline cache to disk (best-effort — read-only installs just skip it).
        void SavePipelineCache() const;

    private:
        std::shared_ptr<VulkanPhysicalDevice> m_PhysicalDevice;
        VkDevice                              m_LogicalDevice;
        VkPipelineCache                       m_PipelineCache = VK_NULL_HANDLE;
        std::string                           m_DeviceName;

        VkQueue m_GraphicsQueue;
        VkQueue m_ComputeQueue;
        VkQueue m_TransferQueue;

        friend class CommandBufferAllocator;
    };
} // namespace Desert::Graphic::API::Vulkan