#include <Engine/Graphic/API/Vulkan/VulkanSwapChain.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanAllocator.hpp>
#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanFramebuffer.hpp>

#include <Engine/Core/EngineContext.hpp>
#include <Engine/Core/FrameManager.hpp>

namespace Desert::Graphic::API::Vulkan
{
    VulkanSwapChain::VulkanSwapChain( const GLFWwindow* window ) : SwapChain( window )
    {
    }

    VulkanSwapChain::~VulkanSwapChain()
    {
        Release();
    }

    void VulkanSwapChain::Init( const VkInstance instance, const std::shared_ptr<Engine::Device>& device )
    {
        const auto vkLogicalDevice = SP_CAST( VulkanLogicalDevice, device );
        m_LogicalDevice            = std::weak_ptr<VulkanLogicalDevice>( vkLogicalDevice );

        InitSurface( const_cast<GLFWwindow*>( m_Window ), instance );
        GetImageFormatAndColorSpace( vkLogicalDevice );
    }

    void VulkanSwapChain::InitSurface( GLFWwindow* window, const VkInstance instance )
    {
        if ( m_Surface == VK_NULL_HANDLE )
        {
            glfwCreateWindowSurface( instance, window, nullptr, &m_Surface );
        }
    }

    Common::ResultStr<bool> VulkanSwapChain::CreateSwapChain( const std::shared_ptr<Engine::Device>& device,
                                                              uint32_t* width, uint32_t* height )
    {
        const auto vkLogicalDevice = SP_CAST( VulkanLogicalDevice, device );
        const auto& lDevice = vkLogicalDevice->GetVulkanLogicalDevice();

        const VkInstance instance =
             SP_CAST( VulkanContext, EngineContext::GetInstance().GetRendererContext() )->GetVulkanInstance();
        
        Init( instance, device );

        if ( m_VkRenderPass == VK_NULL_HANDLE )
        {
            CreateSwapChainRenderPass();
        }

        auto oldSwapchain = m_SwapChain;

        const auto& pDevice = vkLogicalDevice->GetPhysicalDevice()->GetVulkanPhysicalDevice();
        
        VkSurfaceCapabilitiesKHR surfCaps;
        VK_CHECK_RESULT( vkGetPhysicalDeviceSurfaceCapabilitiesKHR( pDevice, m_Surface, &surfCaps ) );

        // maxImageCount == 0 means "no upper limit". Passing hi < lo to std::clamp is undefined
        // behaviour, so substitute the desired count as the ceiling in that case.
        const uint32_t desiredImageCount = surfCaps.minImageCount + 1;
        const uint32_t maxImageCount =
             surfCaps.maxImageCount > 0 ? surfCaps.maxImageCount : desiredImageCount;
        uint32_t numberOfSwapChainImages =
             std::clamp( desiredImageCount, surfCaps.minImageCount, maxImageCount );

        // Pick the present mode from what the surface ACTUALLY supports (hardcoding MAILBOX tripped a
        // validation error on MoltenVK, which offers only FIFO + IMMEDIATE). Preference: MAILBOX
        // (low-latency triple buffering) when available, else FIFO — the only mode the spec guarantees.
        VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;
        {
            uint32_t presentModeCount = 0;
            vkGetPhysicalDeviceSurfacePresentModesKHR( pDevice, m_Surface, &presentModeCount, nullptr );
            std::vector<VkPresentModeKHR> presentModes( presentModeCount );
            if ( presentModeCount > 0 )
                vkGetPhysicalDeviceSurfacePresentModesKHR( pDevice, m_Surface, &presentModeCount,
                                                           presentModes.data() );
            for ( const auto mode : presentModes )
                if ( mode == VK_PRESENT_MODE_MAILBOX_KHR )
                {
                    swapchainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
                    break;
                }
        }

        VkSurfaceTransformFlagsKHR preTransform;
        if ( surfCaps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR )
            preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        else
            preTransform = surfCaps.currentTransform;

        VkExtent2D swapchainExtent = {};
        if ( surfCaps.currentExtent.width == (uint32_t)-1 )
        {
            swapchainExtent.width  = *width;
            swapchainExtent.height = *height;
        }
        else
        {
            swapchainExtent = surfCaps.currentExtent;
            *width          = surfCaps.currentExtent.width;
            *height         = surfCaps.currentExtent.height;
        }

        m_Width  = *width;
        m_Height = *height;

        VkSwapchainCreateInfoKHR swapChainCreateInfo{};
        swapChainCreateInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
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

        VK_CHECK_RESULT( vkCreateSwapchainKHR( lDevice, &swapChainCreateInfo, nullptr, &m_SwapChain ) );

        if ( oldSwapchain != VK_NULL_HANDLE )
        {
            for ( auto& imageView : m_SwapChainImages.ImagesView )
                vkDestroyImageView( lDevice, imageView, nullptr );
            vkDestroySwapchainKHR( lDevice, oldSwapchain, nullptr );
        }

        uint32_t swapChainImagesCount = 0u;
        VK_CHECK_RESULT( vkGetSwapchainImagesKHR( lDevice, m_SwapChain, &swapChainImagesCount, VK_NULL_HANDLE ) );
        m_SwapChainImages.Images.resize( swapChainImagesCount );
        m_SwapChainImages.ImagesView.resize( swapChainImagesCount );

        Engine::FrameManager::GetInstance().Initialize( swapChainImagesCount );
        VK_CHECK_RESULT( vkGetSwapchainImagesKHR( lDevice, m_SwapChain, &swapChainImagesCount, m_SwapChainImages.Images.data() ) );

        for ( uint32_t i = 0; i < swapChainImagesCount; i++ )
        {
            const auto& createdImageView =
                 Utils::CreateImageView( lDevice, m_SwapChainImages.Images[i],
                                         m_ColorFormat, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D, 1U, 1U );
            if ( !createdImageView.IsSuccess() ) return Common::MakeError<bool>( createdImageView.GetError() );
            m_SwapChainImages.ImagesView[i] = createdImageView.GetValue();
        }

        CreateColorAndDepthImages( vkLogicalDevice );
        CreateSwapChainFramebuffers();

        if ( !m_VulkanQueue )
        {
            m_VulkanQueue = std::make_unique<VulkanQueue>( this );
            m_VulkanQueue->Init();
        }

        FramebufferSpecification fbSpec;
        fbSpec.Width = m_Width;
        fbSpec.Height = m_Height;
        fbSpec.DebugName = "SwapchainFramebufferWrapper";
        // BGRA8F to match the actual swapchain colour format (VK_FORMAT_B8G8R8A8_UNORM). This wrapper's render
        // pass is what the runtime builds its present/UI pipelines against, so its attachment format must match
        // the swapchain pass BeginSwapChainRenderPass draws into — else the pipeline is render-pass-incompatible
        // (was (ImageFormat)0 == RGBA8F, tripping VUID-vkCmdDraw-renderPass-02684 on the SwapchainBlit pipeline).
        fbSpec.Attachments.Attachments = { Core::Formats::ImageFormat::BGRA8F };
        fbSpec.PresentTarget           = true; // build its render pass to match the actual present pass
        m_CompositeFramebuffer = std::make_shared<VulkanFramebuffer>( fbSpec );
        std::static_pointer_cast<VulkanFramebuffer>( m_CompositeFramebuffer )->RT_Invalidate();

        return Common::MakeSuccess( true );
    }

    Common::ResultStr<bool>
    VulkanSwapChain::GetImageFormatAndColorSpace( const std::shared_ptr<VulkanLogicalDevice>& device )
    {
        VkPhysicalDevice physicalDevice = device->GetPhysicalDevice()->GetVulkanPhysicalDevice();
        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR( physicalDevice, m_Surface, &formatCount, nullptr );
        if ( !formatCount ) return Common::MakeError<bool>( "null format count" );

        std::vector<VkSurfaceFormatKHR> surfaceFormats( formatCount );
        vkGetPhysicalDeviceSurfaceFormatsKHR( physicalDevice, m_Surface, &formatCount, surfaceFormats.data() );

        if ( ( formatCount == 1 ) && ( surfaceFormats[0].format == VK_FORMAT_UNDEFINED ) )
        {
            m_ColorFormat = VK_FORMAT_B8G8R8A8_UNORM;
            m_ColorSpace  = surfaceFormats[0].colorSpace;
        }
        else
        {
            bool found = false;
            for ( auto&& surfaceFormat : surfaceFormats )
            {
                if ( surfaceFormat.format == VK_FORMAT_B8G8R8A8_UNORM )
                {
                    m_ColorFormat = surfaceFormat.format;
                    m_ColorSpace  = surfaceFormat.colorSpace;
                    found = true;
                    break;
                }
            }
            if ( !found )
            {
                m_ColorFormat = surfaceFormats[0].format;
                m_ColorSpace  = surfaceFormats[0].colorSpace;
            }
        }
        return BOOLSUCCESS;
    }

    Common::ResultStr<VkResult> VulkanSwapChain::AcquireNextImage( VkSemaphore presentCompleteSemaphore,
                                                                   uint32_t*   imageIndex )
    {
        const auto vkLogicalDevice = m_LogicalDevice.lock();
        if ( !vkLogicalDevice ) DESERT_VERIFY( false );

        VK_RETURN_RESULT( vkAcquireNextImageKHR( vkLogicalDevice->GetVulkanLogicalDevice(), m_SwapChain,
                                                 UINT64_MAX, presentCompleteSemaphore, VK_NULL_HANDLE,
                                                 imageIndex ) );
    }

    void VulkanSwapChain::OnResize( uint32_t width, uint32_t height )
    {
        const auto device = m_LogicalDevice.lock();
        if ( !device ) DESERT_VERIFY( false );
        
        Release();
        CreateSwapChain( device, &width, &height );

        auto cmd = CommandBufferAllocator::GetInstance().RT_AllocateCommandBufferGraphic( true ).GetValue();
        for ( auto& image : GetSwapChainVKImage() )
        {
            VkImageMemoryBarrier barrier = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = image, .subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 } };
            vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier );
        }
        CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( cmd );
    }

    void VulkanSwapChain::Release()
    {
        const auto vkLogicalDevice = m_LogicalDevice.lock();
        if ( !vkLogicalDevice ) return;

        const auto& device = vkLogicalDevice->GetVulkanLogicalDevice();
        vkDeviceWaitIdle( device );

        if ( m_SwapChain != VK_NULL_HANDLE )
        {
            for ( auto& view : m_SwapChainImages.ImagesView ) vkDestroyImageView( device, view, nullptr );
            vkDestroySwapchainKHR( device, m_SwapChain, nullptr );
            m_SwapChain = VK_NULL_HANDLE;
        }

        for ( auto fb : m_SwapChainFramebuffers ) vkDestroyFramebuffer( device, fb, nullptr );
        m_SwapChainFramebuffers.clear();

        if ( m_VkRenderPass != VK_NULL_HANDLE )
        {
            vkDestroyRenderPass( device, m_VkRenderPass, nullptr );
            m_VkRenderPass = VK_NULL_HANDLE;
        }

        if ( m_ColorImages.Image )
        {
            VmaAllocator allocator = SP_CAST( VulkanContext, EngineContext::GetInstance().GetRendererContext() )->GetVulkanAllocator()->GetVMAAllocator();
            vmaDestroyImage( allocator, m_ColorImages.Image, (VmaAllocation)m_VmaAllocation[0] );
            vkDestroyImageView( device, m_ColorImages.ImageView, nullptr );
            
            vmaDestroyImage( allocator, m_DepthStencilImages.Image, (VmaAllocation)m_VmaAllocation[1] );
            vkDestroyImageView( device, m_DepthStencilImages.ImageView, nullptr );

            m_VmaAllocation[0] = m_VmaAllocation[1] = nullptr;
            m_ColorImages = {}; m_DepthStencilImages = {};
        }

        m_CompositeFramebuffer = nullptr;
    }

    uint32_t VulkanSwapChain::GetCurrentBufferIndex() const { return m_VulkanQueue->GetImageIndex(); }
    void VulkanSwapChain::PrepareFrame() { m_VulkanQueue->PrepareFrame(); }
    void VulkanSwapChain::Present() { m_VulkanQueue->Present(); }

    Common::ResultStr<VkResult> VulkanSwapChain::CreateSwapChainFramebuffers()
    {
        const auto vkLogicalDevice = m_LogicalDevice.lock();
        if ( !vkLogicalDevice ) DESERT_VERIFY( false );

        m_SwapChainFramebuffers.resize( m_SwapChainImages.ImagesView.size() );
        for ( uint32_t i = 0; i < m_SwapChainFramebuffers.size(); i++ )
        {
            VkImageView attachments[] = { m_SwapChainImages.ImagesView[i] };
            VkFramebufferCreateInfo fbCreateInfo = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = m_VkRenderPass, .attachmentCount = 1, .pAttachments = attachments, .width = m_Width, .height = m_Height, .layers = 1 };
            VK_CHECK_RESULT( vkCreateFramebuffer( vkLogicalDevice->GetVulkanLogicalDevice(), &fbCreateInfo, NULL, &m_SwapChainFramebuffers[i] ) );
        }
        return Common::MakeSuccess( VK_SUCCESS );
    }

    Common::ResultStr<VkResult> VulkanSwapChain::CreateSwapChainRenderPass()
    {
        const auto vkLogicalDevice = m_LogicalDevice.lock();
        if ( !vkLogicalDevice ) DESERT_VERIFY( false );

        VkAttachmentDescription attachment = { .format = m_ColorFormat, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };
        VkAttachmentReference colorRef = { .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription subpass = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &colorRef };
        VkSubpassDependency dependency = { .srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0, .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, .srcAccessMask = 0, .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT };

        VkRenderPassCreateInfo rpInfo = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1, .pAttachments = &attachment, .subpassCount = 1, .pSubpasses = &subpass, .dependencyCount = 1, .pDependencies = &dependency };
        VK_RETURN_RESULT( vkCreateRenderPass( vkLogicalDevice->GetVulkanLogicalDevice(), &rpInfo, nullptr, &m_VkRenderPass ) );
    }

    Common::ResultStr<VkResult> VulkanSwapChain::CreateColorAndDepthImages( const std::shared_ptr<VulkanLogicalDevice>& device )
    {
        VmaAllocator allocator = SP_CAST( VulkanContext, EngineContext::GetInstance().GetRendererContext() )->GetVulkanAllocator()->GetVMAAllocator();
        
        // Color
        VkImageCreateInfo cInfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = m_ColorFormat, .extent = { m_Width, m_Height, 1 }, .mipLevels = 1, .arrayLayers = 1, .samples = m_MSAASamples, .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
        VmaAllocationCreateInfo cAllocInfo = { .usage = VMA_MEMORY_USAGE_GPU_ONLY };
        VK_CHECK_RESULT( vmaCreateImage( allocator, &cInfo, &cAllocInfo, &m_ColorImages.Image, (VmaAllocation*)&m_VmaAllocation[0], nullptr ) );
        
        m_ColorImages.ImageView = Utils::CreateImageView( device->GetVulkanLogicalDevice(), m_ColorImages.Image, m_ColorFormat, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D, 1, 1 ).GetValue();

        // Depth
        VkFormat dFormat = device->GetPhysicalDevice()->GetDepthFormat();
        VkImageCreateInfo dInfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = dFormat, .extent = { m_Width, m_Height, 1 }, .mipLevels = 1, .arrayLayers = 1, .samples = m_MSAASamples, .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
        VmaAllocationCreateInfo dAllocInfo = { .usage = VMA_MEMORY_USAGE_GPU_ONLY };
        VK_CHECK_RESULT( vmaCreateImage( allocator, &dInfo, &dAllocInfo, &m_DepthStencilImages.Image, (VmaAllocation*)&m_VmaAllocation[1], nullptr ) );
        
        m_DepthStencilImages.ImageView = Utils::CreateImageView( device->GetVulkanLogicalDevice(), m_DepthStencilImages.Image, dFormat, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_VIEW_TYPE_2D, 1, 1 ).GetValue();

        return Common::MakeSuccess( VK_SUCCESS );
    }

} // namespace Desert::Graphic::API::Vulkan
