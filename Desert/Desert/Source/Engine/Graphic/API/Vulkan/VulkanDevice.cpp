#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanHelper.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>

namespace Desert::Graphic::API::Vulkan
{
    namespace
    {
        Common::Result<VkResult> FlushCommandBuffer( VkDevice device, VkCommandPool commandPool,
                                                     VkCommandBuffer commandBuffer, VkQueue queue )
        {
            if ( commandBuffer == VK_NULL_HANDLE )
            {
                return Common::MakeError<VkResult>( "Command buffer is VK_NULL_HANDLE" );
            }

            VK_RETURN_RESULT_IF_FALSE_TYPE( VkResult, vkEndCommandBuffer( commandBuffer ) )

            VkSubmitInfo submitInfo       = {};
            submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers    = &commandBuffer;

            VK_RETURN_RESULT_IF_FALSE_TYPE( VkResult, vkQueueSubmit( queue, 1, &submitInfo, VK_NULL_HANDLE ) );

            vkFreeCommandBuffers( device, commandPool, 1, &commandBuffer ); // TODO: vkResetCommandBuffer
            return Common::MakeSuccess( VK_SUCCESS );
        }
    } // namespace

    Common::Result<bool> VulkanPhysicalDevice::CreateDevice()
    {
        auto&    instance    = VulkanContext::GetInstance().GetVulkanInstance();
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

    Common::Result<VkCommandBuffer> VulkanLogicalDevice::RT_GetCommandBufferCompute( bool begin )
    {
        VkCommandBufferAllocateInfo allocateInfo;
        allocateInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        allocateInfo.commandPool        = m_ComputeCommandPool;

        VkCommandBuffer cmdBuffer;
        VK_RETURN_RESULT_IF_FALSE_TYPE( VkCommandBuffer,
                                        vkAllocateCommandBuffers( m_LogicalDevice, &allocateInfo, &cmdBuffer ) );

        if ( begin )
        {
            VkCommandBufferBeginInfo cmdBufferBeginInfo{};
            cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            VK_RETURN_RESULT_IF_FALSE_TYPE( VkCommandBuffer,
                                            vkBeginCommandBuffer( cmdBuffer, &cmdBufferBeginInfo ) );
        }

        return Common::MakeSuccess( cmdBuffer );
    }

    Common::Result<VkCommandBuffer> VulkanLogicalDevice::RT_GetCommandBufferGraphic( bool begin )
    {
        VkCommandBufferAllocateInfo allocateInfo;
        allocateInfo.pNext              = VK_NULL_HANDLE;
        allocateInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        allocateInfo.commandPool        = m_CommandGraphicPool;

        VkCommandBuffer cmdBuffer;
        VK_RETURN_RESULT_IF_FALSE_TYPE( VkCommandBuffer,
                                        vkAllocateCommandBuffers( m_LogicalDevice, &allocateInfo, &cmdBuffer ) );

        if ( begin )
        {
            VkCommandBufferBeginInfo cmdBufferBeginInfo{};
            cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            VK_RETURN_RESULT_IF_FALSE_TYPE( VkCommandBuffer,
                                            vkBeginCommandBuffer( cmdBuffer, &cmdBufferBeginInfo ) );
        }

        return Common::MakeSuccess( cmdBuffer );
    }

    Common::Result<VkResult> VulkanLogicalDevice::RT_FlushCommandBufferCompute( VkCommandBuffer commandBuffer )
    {
        return FlushCommandBuffer( m_LogicalDevice, m_ComputeCommandPool, commandBuffer, m_ComputeQueue );
    }

    Common::Result<VkResult> VulkanLogicalDevice::RT_FlushCommandBufferGraphic( VkCommandBuffer commandBuffer )
    {
        return FlushCommandBuffer( m_LogicalDevice, m_ComputeCommandPool, commandBuffer, m_GraphicsQueue );
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

} // namespace Desert::Graphic::API::Vulkan