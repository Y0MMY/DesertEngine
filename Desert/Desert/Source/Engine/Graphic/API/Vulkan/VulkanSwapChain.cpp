#include <Engine/Graphic/API/Vulkan/VulkanSwapChain.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanHelper.hpp>

namespace Desert::Graphic::API::Vulkan
{

    void VulkanSwapChain::Init( const VkInstance instance, const std::shared_ptr<VulkanLogicalDevice>& device )
    {
        m_VulkanInstance = instance;
        m_LogicalDevice  = device;
    }

    void VulkanSwapChain::InitSurface( GLFWwindow* window )
    {
        glfwCreateWindowSurface( m_VulkanInstance, window, nullptr, &m_Surface );
    }

    Common::Result<VkResult> VulkanSwapChain::Create( uint32_t width, uint32_t height )
    {
        // Get physical device surface properties and formats
        VkSurfaceCapabilitiesKHR surfCaps;
        VK_RETURN_RESULT_IF_FALSE( vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
             m_LogicalDevice->GetPhysicalDevice()->GetVulkanPhysicalDevice(), m_Surface, &surfCaps ) );

        uint32_t numberOfSwapChainImages =
             std::clamp( surfCaps.minImageCount + 1, surfCaps.minImageCount, surfCaps.maxImageCount );

        VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;

        // Find the transformation of the surface
        VkSurfaceTransformFlagsKHR preTransform;
        if ( surfCaps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR )
        {
            // We prefer a non-rotated transform
            preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        }
        else
        {
            preTransform = surfCaps.currentTransform;
        }

        VkSwapchainCreateInfoKHR swapChainCreateInfo{};
        swapChainCreateInfo.sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapChainCreateInfo.pNext                 = nullptr;
        swapChainCreateInfo.surface               = m_Surface;
        swapChainCreateInfo.minImageCount         = numberOfSwapChainImages;
        swapChainCreateInfo.imageColorSpace       = m_ColorSpace;
        swapChainCreateInfo.imageFormat           = m_ColorFormat;
        swapChainCreateInfo.presentMode           = swapchainPresentMode;
        swapChainCreateInfo.imageExtent           = { width, height };
        swapChainCreateInfo.imageArrayLayers      = 1;
        swapChainCreateInfo.imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapChainCreateInfo.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
        swapChainCreateInfo.queueFamilyIndexCount = 0;
        swapChainCreateInfo.pQueueFamilyIndices   = NULL;
        swapChainCreateInfo.preTransform          = (VkSurfaceTransformFlagBitsKHR)preTransform;
        swapChainCreateInfo.clipped               = VK_TRUE;
        swapChainCreateInfo.compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapChainCreateInfo.oldSwapchain          = VK_NULL_HANDLE;

        VK_RETURN_RESULT_IF_FALSE( vkCreateSwapchainKHR( m_LogicalDevice->GetVulkanLogicalDevice(),
                                                         &swapChainCreateInfo, nullptr, &m_SwapChain ) );

        LOG_TRACE( "Swap chain created" );

        uint32_t swapChainImages = 0u;
        VK_RETURN_RESULT_IF_FALSE( vkGetSwapchainImagesKHR( m_LogicalDevice->GetVulkanLogicalDevice(), m_SwapChain,
                                                            &swapChainImages, VK_NULL_HANDLE ) );
        m_Images.resize( swapChainImages );
        m_ImagesView.resize( swapChainImages );

        VK_RETURN_RESULT_IF_FALSE( vkGetSwapchainImagesKHR( m_LogicalDevice->GetVulkanLogicalDevice(), m_SwapChain,
                                                            &swapChainImages, m_Images.data() ) );

        for (uint32_t i = 0; i < swapChainImages; i++)
        {

        }

        return Common::MakeSuccess( VK_SUCCESS );
    }

    Common::Result<VkSurfaceFormatKHR> VulkanSwapChain::GetImageFormatAndColorSpace()
    {
        VkPhysicalDevice physicalDevice = m_LogicalDevice->GetPhysicalDevice()->GetVulkanPhysicalDevice();

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR( physicalDevice, m_Surface, &formatCount, nullptr );

        if ( !formatCount )
        {
            return Common::MakeError<VkSurfaceFormatKHR>( "null format count" );
        }

        std::vector<VkSurfaceFormatKHR> surfaceFormats( formatCount );
        vkGetPhysicalDeviceSurfaceFormatsKHR( physicalDevice, m_Surface, &formatCount, surfaceFormats.data() );

        // If the surface format list only includes one entry with VK_FORMAT_UNDEFINED,
        // there is no preferered format, so we assume VK_FORMAT_B8G8R8A8_UNORM
        if ( ( formatCount == 1 ) && ( surfaceFormats[0].format == VK_FORMAT_UNDEFINED ) ) [[unlikely]]
        {
            m_ColorFormat = VK_FORMAT_B8G8R8A8_UNORM;
            m_ColorSpace  = surfaceFormats[0].colorSpace;
        }
        else [[likely]]
        {
            // iterate over the list of available surface format and
            // check for the presence of VK_FORMAT_B8G8R8A8_UNORM
            bool found_B8G8R8A8_UNORM = false;
            for ( auto&& surfaceFormat : surfaceFormats )
            {
                if ( surfaceFormat.format == VK_FORMAT_B8G8R8A8_UNORM )
                {
                    m_ColorFormat        = surfaceFormat.format;
                    m_ColorSpace         = surfaceFormat.colorSpace;
                    found_B8G8R8A8_UNORM = true;
                    break;
                }
            }

            // in case VK_FORMAT_B8G8R8A8_UNORM is not available
            // select the first available color format
            if ( !found_B8G8R8A8_UNORM )
            {
                m_ColorFormat = surfaceFormats[0].format;
                m_ColorSpace  = surfaceFormats[0].colorSpace;
            }
        }
    }

} // namespace Desert::Graphic::API::Vulkan