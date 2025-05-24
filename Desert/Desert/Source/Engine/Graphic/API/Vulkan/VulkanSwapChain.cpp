#include <Engine/Graphic/API/Vulkan/VulkanSwapChain.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanAllocator.hpp>
#include <Engine/Graphic/Framebuffer.hpp>

#include <Engine/Core/EngineContext.h>

namespace Desert::Graphic::API::Vulkan
{
    void VulkanSwapChain::Init( GLFWwindow* window, const VkInstance instance, VulkanLogicalDevice& device )
    {
        m_VulkanInstance = instance;
        m_LogicalDevice  = &device;

        InitSurface( window );
        GetImageFormatAndColorSpace();

        CreateSwapChainRenderPass();
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

        m_Width  = *width;
        m_Height = *height;

        VkSwapchainCreateInfoKHR swapChainCreateInfo{};
        swapChainCreateInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapChainCreateInfo.pNext            = nullptr;
        swapChainCreateInfo.surface          = m_Surface;
        swapChainCreateInfo.minImageCount    = numberOfSwapChainImages;
        swapChainCreateInfo.imageColorSpace  = m_ColorSpace;
        swapChainCreateInfo.imageFormat      = m_ColorFormat;
        swapChainCreateInfo.presentMode      = swapchainPresentMode;
        swapChainCreateInfo.imageExtent      = swapchainExtent;
        swapChainCreateInfo.imageArrayLayers = 1;
        swapChainCreateInfo.imageUsage = ( VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );
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
            for ( uint32_t i = 0; i < m_SwapChainImages.ImagesView.size(); i++ )
            {
                vkDestroyImageView( lDevice, m_SwapChainImages.ImagesView[i], nullptr );
            }
            vkDestroySwapchainKHR( lDevice, oldSwapchain, nullptr );
        }

        LOG_TRACE( "Swap chain created" );

        uint32_t swapChainImages = 0u;
        VK_RETURN_RESULT_IF_FALSE( vkGetSwapchainImagesKHR( m_LogicalDevice->GetVulkanLogicalDevice(), m_SwapChain,
                                                            &swapChainImages, VK_NULL_HANDLE ) );
        m_SwapChainImages.Images.resize( swapChainImages );
        m_SwapChainImages.ImagesView.resize( swapChainImages );

        EngineContext::GetInstance().m_FramesInFlight = m_SwapChainImages.ImagesView.size();
        VK_RETURN_RESULT_IF_FALSE( vkGetSwapchainImagesKHR( m_LogicalDevice->GetVulkanLogicalDevice(), m_SwapChain,
                                                            &swapChainImages, m_SwapChainImages.Images.data() ) );

        for ( uint32_t i = 0; i < swapChainImages; i++ )
        {
            const auto& createdImageView =
                 Utils::CreateImageView( m_LogicalDevice->GetVulkanLogicalDevice(), m_SwapChainImages.Images[i],
                                         m_ColorFormat, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D, 1U, 1U );
            if ( !createdImageView.IsSuccess() )
            {
                return Common::MakeError<VkResult>( createdImageView.GetError() );
            }

            m_SwapChainImages.ImagesView[i] = createdImageView.GetValue();
        }

        CreateColorAndDepthImages();
        CreateSwapChainFramebuffers();

        return Common::MakeSuccess( VK_SUCCESS );
    }

    Common::Result<bool> VulkanSwapChain::GetImageFormatAndColorSpace()
    {
        VkPhysicalDevice physicalDevice = m_LogicalDevice->GetPhysicalDevice()->GetVulkanPhysicalDevice();

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR( physicalDevice, m_Surface, &formatCount, nullptr );

        if ( !formatCount )
        {
            return Common::MakeError<bool>( "null format count" );
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

        return BOOLSUCCESS;
    }

    Common::Result<VkResult> VulkanSwapChain::AcquireNextImage( VkSemaphore presentCompleteSemaphore,
                                                                uint32_t*   imageIndex )
    {
        VK_RETURN_RESULT( vkAcquireNextImageKHR( m_LogicalDevice->GetVulkanLogicalDevice(), m_SwapChain,
                                                 UINT64_MAX, presentCompleteSemaphore, VK_NULL_HANDLE,
                                                 imageIndex ) );
    }

    void VulkanSwapChain::OnResize( uint32_t width, uint32_t height )
    {

        Release();
        Create( &width, &height );
    }

    void VulkanSwapChain::Release()
    {
        const auto& device = m_LogicalDevice->GetVulkanLogicalDevice();
        if ( m_SwapChain != VK_NULL_HANDLE )
        {
            for ( uint32_t i = 0; i < m_SwapChainImages.ImagesView.size(); i++ )
            {
                vkDestroyImageView( device, m_SwapChainImages.ImagesView[i], nullptr );
            }
        }
        if ( m_Surface != VK_NULL_HANDLE )
        {
            vkDestroySwapchainKHR( device, m_SwapChain, nullptr );
        }

        for ( const auto framebuffer : m_SwapChainFramebuffers )
        {
            vkDestroyFramebuffer( device, framebuffer, VK_NULL_HANDLE );
        }

        m_SwapChain = VK_NULL_HANDLE;
    }

    Common::Result<VkResult> VulkanSwapChain::CreateSwapChainFramebuffers()
    {
        m_SwapChainFramebuffers.resize( m_SwapChainImages.ImagesView.size() );
        for ( uint32_t i = 0; i < m_SwapChainFramebuffers.size(); i++ )
        {
            std::array<VkImageView, 1> attachments = { GetSwapChainVKImagesView()[i] };

            VkFramebufferCreateInfo fbCreateInfo = {};
            fbCreateInfo.sType                   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbCreateInfo.renderPass              = m_VkRenderPass;
            fbCreateInfo.attachmentCount         = static_cast<uint32_t>( attachments.size() );
            fbCreateInfo.pAttachments            = attachments.data();
            fbCreateInfo.width                   = m_Width;
            fbCreateInfo.height                  = m_Height;
            fbCreateInfo.layers                  = 1;

            const auto& device = m_LogicalDevice->GetVulkanLogicalDevice();
            auto        res    = vkCreateFramebuffer( device, &fbCreateInfo, NULL, &m_SwapChainFramebuffers[i] );

            if ( res != VK_SUCCESS )
            {
                return Common::MakeError<VkResult>( "TODO: make error info" );
            }
        }
        return Common::MakeSuccess( VK_SUCCESS );
    }

    Common::Result<VkResult> VulkanSwapChain::CreateSwapChainRenderPass()
    {
        VkFormat depthFormat = m_LogicalDevice->GetPhysicalDevice()->GetDepthFormat();

        // Render Pass
        std::array<VkAttachmentDescription, 1> attachments = {};

        // Color attachment
        attachments[0].format         = m_ColorFormat;
        attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorReference = {};
        colorReference.attachment            = 0;
        colorReference.layout                = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpassDescription = {};
        subpassDescription.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription.colorAttachmentCount = 1;
        subpassDescription.pColorAttachments    = &colorReference;
        subpassDescription.inputAttachmentCount = 0;
        subpassDescription.pInputAttachments    = nullptr;
        subpassDescription.pPreserveAttachments = nullptr;
        subpassDescription.pResolveAttachments  = nullptr;

        VkSubpassDependency dependency = {};
        dependency.srcSubpass          = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass          = 0;
        dependency.srcStageMask        = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask        = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask       = 0;
        dependency.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType                  = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1; 
        renderPassInfo.pAttachments    = attachments.data();
        renderPassInfo.subpassCount    = 1;
        renderPassInfo.pSubpasses      = &subpassDescription;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies   = &dependency;

        VK_RETURN_RESULT( vkCreateRenderPass( m_LogicalDevice->GetVulkanLogicalDevice(), &renderPassInfo,
                                                       nullptr, &m_VkRenderPass ) );
    }

    Common::Result<VkResult> VulkanSwapChain::CreateColorAndDepthImages()
    {
        // Color image
        {
            VkImageCreateInfo imageInfo{};
            imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType     = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width  = m_Width;
            imageInfo.extent.height = m_Height;
            imageInfo.extent.depth  = 1;
            imageInfo.mipLevels     = 1;
            imageInfo.arrayLayers   = 1;
            imageInfo.format        = m_ColorFormat;
            imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage       = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            imageInfo.samples     = m_MSAASamples;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VulkanAllocator::GetInstance().RT_AllocateImage( "sdf", imageInfo, VMA_MEMORY_USAGE_GPU_ONLY,
                                                             m_ColorImages.Image );

            VkImageViewCreateInfo imageViewCI{};
            imageViewCI.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            imageViewCI.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            imageViewCI.image                           = m_ColorImages.Image;
            imageViewCI.format                          = m_ColorFormat;
            imageViewCI.subresourceRange.baseMipLevel   = 0;
            imageViewCI.subresourceRange.levelCount     = 1;
            imageViewCI.subresourceRange.baseArrayLayer = 0;
            imageViewCI.subresourceRange.layerCount     = 1;
            imageViewCI.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;

            VK_CHECK_RESULT( vkCreateImageView( m_LogicalDevice->GetVulkanLogicalDevice(), &imageViewCI, nullptr,
                                                &m_ColorImages.ImageView ) );
        }

        // Depth image
        {
            VkFormat depthFormat = m_LogicalDevice->GetPhysicalDevice()->GetDepthFormat();

            VkImageCreateInfo imageInfo{};
            imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType     = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width  = m_Width;
            imageInfo.extent.height = m_Height;
            imageInfo.extent.depth  = 1;
            imageInfo.mipLevels     = 1;
            imageInfo.arrayLayers   = 1;
            imageInfo.format        = depthFormat;
            imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            imageInfo.samples       = m_MSAASamples;
            imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

            VulkanAllocator::GetInstance().RT_AllocateImage( "sdf", imageInfo, VMA_MEMORY_USAGE_GPU_ONLY,
                                                             m_DepthStencilImages.Image );

            VkImageViewCreateInfo imageViewCI{};
            imageViewCI.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            imageViewCI.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            imageViewCI.image                           = m_DepthStencilImages.Image;
            imageViewCI.format                          = depthFormat;
            imageViewCI.subresourceRange.baseMipLevel   = 0;
            imageViewCI.subresourceRange.levelCount     = 1;
            imageViewCI.subresourceRange.baseArrayLayer = 0;
            imageViewCI.subresourceRange.layerCount     = 1;
            imageViewCI.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;

            VK_CHECK_RESULT( vkCreateImageView( m_LogicalDevice->GetVulkanLogicalDevice(), &imageViewCI, nullptr,
                                                &m_DepthStencilImages.ImageView ) );
        }

        return Common::MakeSuccess( VK_SUCCESS );
    }

} // namespace Desert::Graphic::API::Vulkan