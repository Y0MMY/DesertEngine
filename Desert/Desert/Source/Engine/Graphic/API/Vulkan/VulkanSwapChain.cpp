#include <Engine/Graphic/API/Vulkan/VulkanSwapChain.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanHelper.hpp>

namespace Desert::Graphic::API::Vulkan
{
    namespace
    {
        Common::Result<VkImageView> CreateImageView( VkDevice device, VkImage image, VkFormat format,
                                                     VkImageAspectFlags aspectFlags, VkImageViewType viewType,
                                                     uint32_t layerCount, uint32_t mipLeveles )
        {
            VkImageViewCreateInfo viewInfo = { .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                               .pNext            = VK_NULL_HANDLE,
                                               .image            = image,
                                               .viewType         = viewType,
                                               .format           = format,
                                               .components       = { .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                                                                     .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                                                                     .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                                                                     .a = VK_COMPONENT_SWIZZLE_IDENTITY },
                                               .subresourceRange = { .aspectMask     = aspectFlags,
                                                                     .baseMipLevel   = 0,
                                                                     .levelCount     = mipLeveles,
                                                                     .baseArrayLayer = 0,
                                                                     .layerCount     = layerCount } };

            VkImageView imageView;

            VK_RETURN_RESULT_IF_FALSE_TYPE( VkImageView,
                                            vkCreateImageView( device, &viewInfo, VK_NULL_HANDLE, &imageView ) );

            return Common::MakeSuccess( imageView );
        }
    } // namespace
    void VulkanSwapChain::Init( GLFWwindow* window, const VkInstance instance, VulkanLogicalDevice& device )
    {
        m_VulkanInstance = instance;
        m_LogicalDevice  = &device;

        InitSurface( window );
        GetImageFormatAndColorSpace();
    }

    void VulkanSwapChain::InitSurface( GLFWwindow* window )
    {
        glfwCreateWindowSurface( m_VulkanInstance, window, nullptr, &m_Surface );
    }

    Common::Result<VkResult> VulkanSwapChain::Create( uint32_t* width, uint32_t* height )
    {
        auto oldSwapchain = m_SwapChain;

        const auto& pDevice = m_LogicalDevice->GetPhysicalDevice()->GetVulkanPhysicalDevice();
        const auto& lDevice = m_LogicalDevice->GetVulkanLogicalDevice();
        // Get physical device surface properties and formats
        VkSurfaceCapabilitiesKHR surfCaps;
        VK_RETURN_RESULT_IF_FALSE( vkGetPhysicalDeviceSurfaceCapabilitiesKHR( pDevice, m_Surface, &surfCaps ) );

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

        VkExtent2D swapchainExtent = {};
        // If width (and height) equals the special value 0xFFFFFFFF, the size of the surface will be set by the
        // swapchain
        if ( surfCaps.currentExtent.width == (uint32_t)-1 )
        {
            // If the surface size is undefined, the size is set to
            // the size of the images requested.
            swapchainExtent.width  = *width;
            swapchainExtent.height = *height;
        }
        else
        {
            // If the surface size is defined, the swap chain size must match
            swapchainExtent = surfCaps.currentExtent;
            *width          = surfCaps.currentExtent.width;
            *height         = surfCaps.currentExtent.height;
        }

        VkSwapchainCreateInfoKHR swapChainCreateInfo{};
        swapChainCreateInfo.sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapChainCreateInfo.pNext                 = nullptr;
        swapChainCreateInfo.surface               = m_Surface;
        swapChainCreateInfo.minImageCount         = numberOfSwapChainImages;
        swapChainCreateInfo.imageColorSpace       = m_ColorSpace;
        swapChainCreateInfo.imageFormat           = m_ColorFormat;
        swapChainCreateInfo.presentMode           = swapchainPresentMode;
        swapChainCreateInfo.imageExtent           = swapchainExtent;
        swapChainCreateInfo.imageArrayLayers      = 1;
        swapChainCreateInfo.imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapChainCreateInfo.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
        swapChainCreateInfo.queueFamilyIndexCount = 0;
        swapChainCreateInfo.pQueueFamilyIndices   = NULL;
        swapChainCreateInfo.preTransform          = (VkSurfaceTransformFlagBitsKHR)preTransform;
        swapChainCreateInfo.clipped               = VK_TRUE;
        swapChainCreateInfo.compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapChainCreateInfo.oldSwapchain          = oldSwapchain;

        VK_RETURN_RESULT_IF_FALSE( vkCreateSwapchainKHR( m_LogicalDevice->GetVulkanLogicalDevice(),
                                                         &swapChainCreateInfo, nullptr, &m_SwapChain ) );

        // If an existing swap chain is re-created, destroy the old swap chain
        // This also cleans up all the presentable images
        if ( oldSwapchain != VK_NULL_HANDLE )
        {
            for ( uint32_t i = 0; i < m_ImagesView.size(); i++ )
            {
                vkDestroyImageView( lDevice, m_ImagesView[i], nullptr );
            }
            vkDestroySwapchainKHR( lDevice, oldSwapchain, nullptr );
        }

        LOG_TRACE( "Swap chain created" );

        uint32_t swapChainImages = 0u;
        VK_RETURN_RESULT_IF_FALSE( vkGetSwapchainImagesKHR( m_LogicalDevice->GetVulkanLogicalDevice(), m_SwapChain,
                                                            &swapChainImages, VK_NULL_HANDLE ) );
        m_Images.resize( swapChainImages );
        m_ImagesView.resize( swapChainImages );

        VK_RETURN_RESULT_IF_FALSE( vkGetSwapchainImagesKHR( m_LogicalDevice->GetVulkanLogicalDevice(), m_SwapChain,
                                                            &swapChainImages, m_Images.data() ) );

        for ( uint32_t i = 0; i < swapChainImages; i++ )
        {
            const auto& createdImageView =
                 CreateImageView( m_LogicalDevice->GetVulkanLogicalDevice(), m_Images[i], m_ColorFormat,
                                  VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D, 1U, 1U );
            if ( !createdImageView.IsSuccess() )
            {
                return Common::MakeError<VkResult>( createdImageView.GetError() );
            }

            m_ImagesView[i] = createdImageView.GetValue();
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

    Common::Result<VkResult> VulkanSwapChain::AcquireNextImage( VkSemaphore presentCompleteSemaphore,
                                                                uint32_t*   imageIndex )
    {
        VK_RETURN_RESULT( vkAcquireNextImageKHR( m_LogicalDevice->GetVulkanLogicalDevice(), m_SwapChain,
                                                 UINT64_MAX, presentCompleteSemaphore, VK_NULL_HANDLE,
                                                 imageIndex ) );
    }

   

} // namespace Desert::Graphic::API::Vulkan