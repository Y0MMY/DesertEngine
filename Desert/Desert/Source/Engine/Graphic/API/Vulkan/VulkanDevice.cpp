#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Graphic/Renderer.hpp>

namespace Desert::Graphic::API::Vulkan
{
    Common::Result<bool> VulkanPhysicalDevice::CreateDevice()
    {
        auto& instance = static_cast<VulkanContext*>( Renderer::GetInstance().GetRendererContext().get() )
                              ->GetVulkanInstance();
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices( instance, &deviceCount, nullptr );
        DESERT_VERIFY( deviceCount, "Failed to find GPUs with Vulkan support!" );
        std::vector<VkPhysicalDevice> devices( deviceCount );
        vkEnumeratePhysicalDevices( instance, &deviceCount, devices.data() );

        VkPhysicalDevice           selectedPhysicalDevice = nullptr;
        VkPhysicalDeviceProperties deviceProperties;
        VkPhysicalDeviceFeatures   deviceFeatures;

        auto isDeviceSuitable = [&deviceProperties,
                                 &deviceFeatures]( VkPhysicalDevice device )
             -> bool // NOTE: Lambda function to check is device suitable
        {
            vkGetPhysicalDeviceProperties( device, &deviceProperties );
            vkGetPhysicalDeviceFeatures( device, &deviceFeatures );

            return deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
                   deviceFeatures.geometryShader;
        };

        for ( const auto& device : devices )
        {
            if ( isDeviceSuitable( device ) )
            {
                selectedPhysicalDevice = device;
                break;
            }
        }

        if ( !selectedPhysicalDevice )
        {
            DESERT_VERIFY( "Could not find discrete GPU." );
            selectedPhysicalDevice = devices.back();
        }

        DESERT_VERIFY( selectedPhysicalDevice, "Could not find any physical devices!" );

        m_PhysicalDevice = selectedPhysicalDevice;

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties( m_PhysicalDevice, &queueFamilyCount, nullptr );

        m_QueueFamilyProperties.resize( queueFamilyCount );
        vkGetPhysicalDeviceQueueFamilyProperties( m_PhysicalDevice, &queueFamilyCount,
                                                  m_QueueFamilyProperties.data() );

        int requestedQueueTypes = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        m_QueueFamilyIndices    = GetQueueFamilyIndices( requestedQueueTypes );

        static constexpr float queuePriority = 1.0f;

        if ( requestedQueueTypes & VK_QUEUE_GRAPHICS_BIT )
        {
            // Graphics queue
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = m_QueueFamilyIndices.GraphicsFamily.value_or( -1 );
            queueCreateInfo.queueCount       = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            m_QueueCreateInfos.push_back( queueCreateInfo );
        }

        if ( requestedQueueTypes & VK_QUEUE_COMPUTE_BIT )
        {
            if ( m_QueueFamilyIndices.ComputeFamily != m_QueueFamilyIndices.GraphicsFamily )
            {
                // Graphics queue
                VkDeviceQueueCreateInfo queueCreateInfo{};
                queueCreateInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queueCreateInfo.queueFamilyIndex = m_QueueFamilyIndices.ComputeFamily.value_or( -1 );
                queueCreateInfo.queueCount       = 1;
                queueCreateInfo.pQueuePriorities = &queuePriority;
                m_QueueCreateInfos.push_back( queueCreateInfo );
            }
        }

        // Check for ext.

        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties( m_PhysicalDevice, nullptr, &extensionCount, nullptr );

        if ( extensionCount )
        {
            std::vector<VkExtensionProperties> availableExtensions( extensionCount );
            vkEnumerateDeviceExtensionProperties( m_PhysicalDevice, nullptr, &extensionCount,
                                                  availableExtensions.data() );

            DESERT_VERIFY( "Selected physical device has {0} extensions", extensions.size() );
            for ( const auto& ext : availableExtensions )
            {
                m_SupportedExtensions.emplace( ext.extensionName );
            }
        }

        m_DepthFormat = FindDepthFormat();

        return Common::MakeSuccess( true );
    }

    std::shared_ptr<Desert::Graphic::API::Vulkan::VulkanPhysicalDevice> VulkanPhysicalDevice::Create()
    {
        return std::make_shared<Desert::Graphic::API::Vulkan::VulkanPhysicalDevice>();
    }

    VulkanLogicalDevice::VulkanLogicalDevice( const std::shared_ptr<VulkanPhysicalDevice>& physicalDevice )
         : m_PhysicalDevice( physicalDevice )
    {
    }

    void VulkanLogicalDevice::Destroy()
    {
        vkDeviceWaitIdle( m_LogicalDevice );
        vkDestroyDevice( m_LogicalDevice, nullptr );
    }

    Common::Result<bool> VulkanLogicalDevice::CreateDevice()
    {
        VkDeviceCreateInfo       createInfo{};
        VkPhysicalDeviceFeatures deviceFeatures{};
        createInfo.sType                 = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pQueueCreateInfos     = m_PhysicalDevice->m_QueueCreateInfos.data();
        createInfo.queueCreateInfoCount  = m_PhysicalDevice->m_QueueCreateInfos.size();
        createInfo.pEnabledFeatures      = &deviceFeatures;
        createInfo.enabledExtensionCount = 0;

        std::vector<const char*> deviceExtensions;
        DESERT_VERIFY( m_PhysicalDevice->IsExtensionSupported( VK_KHR_SWAPCHAIN_EXTENSION_NAME ) );
        deviceExtensions.push_back( VK_KHR_SWAPCHAIN_EXTENSION_NAME );

        if ( deviceExtensions.size() > 0 )
        {
            createInfo.ppEnabledExtensionNames = deviceExtensions.data();
            createInfo.enabledExtensionCount   = (uint32_t)deviceExtensions.size();
        }

        VK_CHECK_RESULT( vkCreateDevice( m_PhysicalDevice->GetVulkanPhysicalDevice(), &createInfo, nullptr,
                                         &m_LogicalDevice ) );
        VkCommandPoolCreateInfo cmdPoolInfo = {};
        cmdPoolInfo.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmdPoolInfo.queueFamilyIndex        = *m_PhysicalDevice->m_QueueFamilyIndices.GraphicsFamily;
        cmdPoolInfo.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        VK_CHECK_RESULT( vkCreateCommandPool( m_LogicalDevice, &cmdPoolInfo, nullptr, &m_CommandGraphicPool ) );

        cmdPoolInfo.queueFamilyIndex = *m_PhysicalDevice->m_QueueFamilyIndices.ComputeFamily;
        VK_CHECK_RESULT( vkCreateCommandPool( m_LogicalDevice, &cmdPoolInfo, nullptr, &m_ComputeCommandPool ) );

        vkGetDeviceQueue( m_LogicalDevice, *m_PhysicalDevice->m_QueueFamilyIndices.GraphicsFamily, 0,
                          &m_GraphicsQueue );
        vkGetDeviceQueue( m_LogicalDevice, *m_PhysicalDevice->m_QueueFamilyIndices.ComputeFamily, 0,
                          &m_ComputeQueue );

        return Common::MakeSuccess( true );
    }

    VulkanPhysicalDevice::QueueFamilyIndices VulkanPhysicalDevice::GetQueueFamilyIndices( int flags )
    {
        QueueFamilyIndices indices;

        // Dedicated queue for compute
        // Try to find a queue family index that supports compute but not graphics
        if ( flags & VK_QUEUE_COMPUTE_BIT )
        {
            for ( uint32_t i = 0; i < m_QueueFamilyProperties.size(); i++ )
            {
                auto& queueFamilyProperties = m_QueueFamilyProperties[i];
                if ( ( queueFamilyProperties.queueFlags & VK_QUEUE_COMPUTE_BIT ) &&
                     ( ( queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT ) == 0 ) )
                {
                    indices.ComputeFamily = i;
                    break;
                }
            }
        }

        // For other queue types or if no separate compute queue is present, return the first one to support the
        // requested flags
        for ( uint32_t i = 0; i < m_QueueFamilyProperties.size(); i++ )
        {
            if ( ( flags & VK_QUEUE_COMPUTE_BIT ) && !indices.ComputeFamily )
            {
                if ( m_QueueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT )
                    indices.ComputeFamily = i;
            }

            if ( flags & VK_QUEUE_GRAPHICS_BIT )
            {
                if ( m_QueueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT )
                    indices.GraphicsFamily = i;
            }
        }

        return indices;
    }

    VkFormat VulkanPhysicalDevice::FindDepthFormat() const
    {
        // Since all depth formats may be optional, we need to find a suitable depth format to use
        // Start with the highest precision packed format
        std::array<VkFormat, 5> depthFormats = { VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D32_SFLOAT,
                                                 VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM_S8_UINT,
                                                 VK_FORMAT_D16_UNORM };

        for ( auto& format : depthFormats )
        {
            VkFormatProperties formatProps;
            vkGetPhysicalDeviceFormatProperties( m_PhysicalDevice, format, &formatProps );
            // Format must support depth stencil attachment for optimal tiling
            if ( formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT )
                return format;
        }
        return VK_FORMAT_UNDEFINED;
    }
} // namespace Desert::Graphic::API::Vulkan