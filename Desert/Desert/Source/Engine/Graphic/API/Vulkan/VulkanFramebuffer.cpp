#include <Engine/Graphic/API/Vulkan/VulkanFramebuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>
#include <Engine/Graphic/Renderer.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>
#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Core/EngineContext.hpp>

#include <Engine/Graphic/Image.hpp>

namespace Desert::Graphic::API::Vulkan
{
    namespace
    {
        VkAttachmentLoadOp GetVkAttachmentLoadOp( AttachmentLoad load )
        {
            switch ( load )
            {
                case AttachmentLoad::Clear:
                    return VK_ATTACHMENT_LOAD_OP_CLEAR;
                case AttachmentLoad::Load:
                    return VK_ATTACHMENT_LOAD_OP_LOAD;
                default:
                    return VK_ATTACHMENT_LOAD_OP_CLEAR;
            }
        }

        Common::ResultStr<VkRenderPass>
        CreateRenderPass( VkDevice device, const std::vector<Core::Formats::ImageFormat>& colorAttachments,
                          const std::optional<Core::Formats::ImageFormat>& depthAttachment,
                          const std::vector<ExternalAttachment>&           externalColorAttachments,
                          const std::optional<ExternalAttachment>&         externalDepthAttachment,
                          std::vector<VkClearValue>&                       clearValues )
        {
            std::vector<VkAttachmentDescription> attachmentDescriptions;
            std::vector<VkAttachmentReference>   colorAttachmentRefs;
            std::optional<VkAttachmentReference> depthAttachmentRef;

            // Process internal color attachments
            for ( const auto& format : colorAttachments )
            {
                VkAttachmentDescription desc{};
                desc.format         = GetImageVulkanFormat( format );
                desc.samples        = VK_SAMPLE_COUNT_1_BIT;
                desc.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD; // Always clear for internal
                desc.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
                desc.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
                desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                desc.initialLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                desc.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                VkAttachmentReference ref{};
                ref.attachment = static_cast<uint32_t>( attachmentDescriptions.size() );
                ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                attachmentDescriptions.push_back( desc );
                colorAttachmentRefs.push_back( ref );

                clearValues.emplace_back();
                clearValues.back().color = { 0.1000000015, 0.1000000015, 0.1000000015, 1.00 };
            }

            // Process external color attachments
            for ( const auto& extAttachment : externalColorAttachments )
            {
                if ( auto image = extAttachment.SourceFramebuffer->GetColorAttachmentImage(
                          extAttachment.AttachmentIndex ) )
                {
                    VkAttachmentDescription desc{};
                    desc.format         = GetImageVulkanFormat( image->GetImageSpecification().Format );
                    desc.samples        = VK_SAMPLE_COUNT_1_BIT;
                    desc.loadOp         = GetVkAttachmentLoadOp( extAttachment.Load );
                    desc.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
                    desc.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                    desc.initialLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    desc.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                    VkAttachmentReference ref{};
                    ref.attachment = static_cast<uint32_t>( attachmentDescriptions.size() );
                    ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                    attachmentDescriptions.push_back( desc );
                    colorAttachmentRefs.push_back( ref );

                    clearValues.emplace_back();
                    clearValues.back().color = { 0.1000000015, 0.1000000015, 0.1000000015, 1.00 };
                }
            }

            // Process internal depth attachment
            if ( depthAttachment )
            {
                bool hasStencil = Graphic::Utils::HasStencilComponent( *depthAttachment );

                VkAttachmentDescription desc{};
                desc.format         = GetImageVulkanFormat( *depthAttachment );
                desc.samples        = VK_SAMPLE_COUNT_1_BIT;
                desc.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
                desc.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
                desc.stencilLoadOp  = hasStencil ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                desc.stencilStoreOp = hasStencil ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
                desc.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
                desc.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

                VkAttachmentReference ref{};
                ref.attachment = static_cast<uint32_t>( attachmentDescriptions.size() );
                ref.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

                attachmentDescriptions.push_back( desc );
                depthAttachmentRef = ref;

                clearValues.emplace_back();
                clearValues.back().depthStencil.depth   = 1.0f;
                clearValues.back().depthStencil.stencil = 0;
            }

            // Process external depth attachment
            if ( externalDepthAttachment )
            {
                if ( auto image = externalDepthAttachment->SourceFramebuffer->GetDepthAttachmentImage() )
                {
                    bool hasStencil = Graphic::Utils::HasStencilComponent( image->GetImageSpecification().Format );

                    VkAttachmentDescription desc{};
                    desc.format        = GetImageVulkanFormat( image->GetImageSpecification().Format );
                    desc.samples       = VK_SAMPLE_COUNT_1_BIT;
                    desc.loadOp        = GetVkAttachmentLoadOp( externalDepthAttachment->Load );
                    desc.storeOp       = VK_ATTACHMENT_STORE_OP_STORE;
                    desc.stencilLoadOp = hasStencil ? GetVkAttachmentLoadOp( externalDepthAttachment->Load )
                                                    : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    desc.stencilStoreOp =
                         hasStencil ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
                    desc.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    desc.finalLayout   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

                    VkAttachmentReference ref{};
                    ref.attachment = static_cast<uint32_t>( attachmentDescriptions.size() );
                    ref.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

                    attachmentDescriptions.push_back( desc );
                    depthAttachmentRef = ref;

                    clearValues.emplace_back();
                    clearValues.back().depthStencil.depth   = 1.0f;
                    clearValues.back().depthStencil.stencil = 0;
                }
            }

            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount    = static_cast<uint32_t>( colorAttachmentRefs.size() );
            subpass.pColorAttachments       = colorAttachmentRefs.data();
            subpass.pDepthStencilAttachment = depthAttachmentRef ? &( *depthAttachmentRef ) : nullptr;

            std::vector<VkSubpassDependency> dependencies;

            if ( colorAttachments.size() )
            {
                {
                    VkSubpassDependency& depedency = dependencies.emplace_back();
                    depedency.srcSubpass           = VK_SUBPASS_EXTERNAL;
                    depedency.dstSubpass           = 0;
                    depedency.srcStageMask         = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                    depedency.srcAccessMask        = VK_ACCESS_SHADER_READ_BIT;
                    depedency.dstStageMask         = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                    depedency.dstAccessMask        = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    depedency.dependencyFlags      = VK_DEPENDENCY_BY_REGION_BIT;
                }
                {
                    VkSubpassDependency& depedency = dependencies.emplace_back();
                    depedency.srcSubpass           = 0;
                    depedency.dstSubpass           = VK_SUBPASS_EXTERNAL;
                    depedency.srcStageMask         = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                    depedency.srcAccessMask        = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    depedency.dstStageMask         = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                    depedency.dstAccessMask        = VK_ACCESS_SHADER_READ_BIT;
                    depedency.dependencyFlags      = VK_DEPENDENCY_BY_REGION_BIT;
                }
            }

            if ( depthAttachment || externalDepthAttachment )
            {
                {
                    VkSubpassDependency& depedency = dependencies.emplace_back();
                    depedency.srcSubpass           = VK_SUBPASS_EXTERNAL;
                    depedency.dstSubpass           = 0;
                    depedency.srcStageMask         = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                    depedency.dstStageMask         = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
                    depedency.srcAccessMask        = VK_ACCESS_SHADER_READ_BIT;
                    depedency.dstAccessMask        = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    depedency.dependencyFlags      = VK_DEPENDENCY_BY_REGION_BIT;
                }

                {
                    VkSubpassDependency& depedency = dependencies.emplace_back();
                    depedency.srcSubpass           = 0;
                    depedency.dstSubpass           = VK_SUBPASS_EXTERNAL;
                    depedency.srcStageMask         = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                    depedency.dstStageMask         = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                    depedency.srcAccessMask        = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    depedency.dstAccessMask        = VK_ACCESS_SHADER_READ_BIT;
                    depedency.dependencyFlags      = VK_DEPENDENCY_BY_REGION_BIT;
                }
            }

            VkRenderPassCreateInfo renderPassCreateInfo{};
            renderPassCreateInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassCreateInfo.attachmentCount = static_cast<uint32_t>( attachmentDescriptions.size() );
            renderPassCreateInfo.pAttachments    = attachmentDescriptions.data();
            renderPassCreateInfo.subpassCount    = 1;
            renderPassCreateInfo.pSubpasses      = &subpass;
            renderPassCreateInfo.dependencyCount = static_cast<uint32_t>( dependencies.size() );
            renderPassCreateInfo.pDependencies   = dependencies.data();

            VkRenderPass renderPass;
            if ( vkCreateRenderPass( device, &renderPassCreateInfo, nullptr, &renderPass ) != VK_SUCCESS )
            {
                return Common::MakeError<VkRenderPass>( "Failed to create render pass" );
            }

            return Common::MakeSuccess( renderPass );
        }
    } // namespace

    VulkanFramebuffer::VulkanFramebuffer( const FramebufferSpecification& spec )
         : m_FramebufferSpecification( spec )
    {
        // Separate color and depth attachments during construction
        for ( const auto format : spec.Attachments.Attachments )
        {
            Core::Formats::Image2DSpecification imageSpec{ .Tag        = spec.DebugName,
                                                           .Format     = format,
                                                           .Usage      = Core::Formats::Image2DUsage::Attachment,
                                                           .Properties = Core::Formats::Sample };

            if ( Graphic::Utils::IsDepthFormat( format ) )
            {
                m_DepthAttachment = std::make_shared<VulkanImage2D>( imageSpec );
            }
            else
            {
                m_ColorAttachments.push_back( std::make_shared<VulkanImage2D>( imageSpec ) );
            }
        }

        // Process external attachments
        for ( const auto& extAttachment : spec.ExternalAttachments.ColorAttachments )
        {
            m_ExternalColorAttachments.push_back(
                 { extAttachment.SourceFramebuffer->GetColorAttachmentImage( extAttachment.AttachmentIndex ),
                   extAttachment.Load } );
        }

        if ( spec.ExternalAttachments.DepthAttachment )
        {
            const auto& ext           = *spec.ExternalAttachments.DepthAttachment;
            m_ExternalDepthAttachment = { ext.SourceFramebuffer->GetDepthAttachmentImage(), ext.Load };
        }
    }

    Common::BoolResultStr VulkanFramebuffer::Resize( uint32_t width, uint32_t height, bool forceRecreate )
    {
        if ( m_Framebuffer != VK_NULL_HANDLE )
        {
            Release();
        }

        m_Width  = width;
        m_Height = height;

        // Resize color attachments
        for ( auto& colorAttachment : m_ColorAttachments )
        {
            auto& spec  = colorAttachment->GetImageSpecification();
            spec.Width  = width;
            spec.Height = height;
            sp_cast<VulkanImage2D>( colorAttachment )->RT_Invalidate();
        }

        // Resize depth attachment
        if ( m_DepthAttachment )
        {
            auto& spec  = m_DepthAttachment->GetImageSpecification();
            spec.Width  = width;
            spec.Height = height;
            sp_cast<VulkanImage2D>( m_DepthAttachment )->RT_Invalidate();
        }

        TransitionImagesToInitialLayouts();

        VkDevice device = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetMainDevice() )
                               ->GetVulkanLogicalDevice();
        const auto result = CreateFramebuffer( device, width, height );
        if ( !result.IsSuccess() )
        {
            return Common::MakeError( result.GetError() );
        }

        return Common::MakeSuccess( true );
    }

    Common::ResultStr<VkFramebuffer> VulkanFramebuffer::CreateFramebuffer( VkDevice device, uint32_t width,
                                                                        uint32_t height )
    {
        if ( m_RenderPass == nullptr )
        {
            // Prepare color attachment formats
            std::vector<Core::Formats::ImageFormat> colorFormats;
            for ( const auto& attachment : m_ColorAttachments )
            {
                colorFormats.push_back( attachment->GetImageSpecification().Format );
            }

            // Prepare depth attachment format (if exists)
            std::optional<Core::Formats::ImageFormat> depthFormat;
            if ( m_DepthAttachment )
            {
                depthFormat = m_DepthAttachment->GetImageSpecification().Format;
            }

            const auto renderPassResult =
                 CreateRenderPass( device, colorFormats, depthFormat,
                                   m_FramebufferSpecification.ExternalAttachments.ColorAttachments,
                                   m_FramebufferSpecification.ExternalAttachments.DepthAttachment, m_ClearValues );

            if ( !renderPassResult.IsSuccess() )
            {
                return Common::MakeError<VkFramebuffer>( renderPassResult.GetError() );
            }
            m_RenderPass = renderPassResult.GetValue();
        }

        std::vector<VkImageView> attachments;

        // Add color attachments first
        for ( const auto& colorAttachment : m_ColorAttachments )
        {
            attachments.push_back(
                 sp_cast<VulkanImage2D>( colorAttachment )->GetVulkanImageInfo().ImageInfo.imageView );
        }

        // Add external color attachments
        for ( const auto& externalColorAttachment : m_ExternalColorAttachments )
        {
            if ( const auto& attachment = externalColorAttachment.Image.lock() )
            {
                attachments.push_back(
                     sp_cast<VulkanImage2D>( attachment )->GetVulkanImageInfo().ImageInfo.imageView );
            }
        }

        // Add depth attachment (if exists)
        if ( m_DepthAttachment )
        {
            attachments.push_back(
                 sp_cast<VulkanImage2D>( m_DepthAttachment )->GetVulkanImageInfo().ImageInfo.imageView );
        }

        // Add external depth attachment (if exists)
        if ( m_ExternalDepthAttachment )
        {
            if ( const auto& externalDepthAttachment = m_ExternalDepthAttachment->Image.lock() )
            {
                attachments.push_back(
                     sp_cast<VulkanImage2D>( externalDepthAttachment )->GetVulkanImageInfo().ImageInfo.imageView );
            }
        }

        VkFramebufferCreateInfo framebufferCreateInfo{};
        framebufferCreateInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferCreateInfo.renderPass      = m_RenderPass;
        framebufferCreateInfo.attachmentCount = static_cast<uint32_t>( attachments.size() );
        framebufferCreateInfo.pAttachments    = attachments.data();
        framebufferCreateInfo.width           = width;
        framebufferCreateInfo.height          = height;
        framebufferCreateInfo.layers          = 1;

        if ( vkCreateFramebuffer( device, &framebufferCreateInfo, nullptr, &m_Framebuffer ) != VK_SUCCESS )
        {
            return Common::MakeError<VkFramebuffer>( "Failed to create framebuffer" );
        }

        VKUtils::SetDebugUtilsObjectName( device, VK_OBJECT_TYPE_FRAMEBUFFER,
                                          ( "FB (" + m_FramebufferSpecification.DebugName + ")" ), m_Framebuffer );

        return Common::MakeSuccess( m_Framebuffer );
    }

    Common::BoolResultStr VulkanFramebuffer::Release()
    {
        VkDevice device = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetMainDevice() )
                               ->GetVulkanLogicalDevice();
        vkDeviceWaitIdle( device );
        // Release color attachments
        for ( auto& colorAttachment : m_ColorAttachments )
        {
            colorAttachment->Release();
        }

        // Release depth attachment
        if ( m_DepthAttachment )
        {
            m_DepthAttachment->Release();
        }

        return BOOLSUCCESS;
    }

    VulkanFramebuffer::~VulkanFramebuffer()
    {
        Release();

        VkDevice device = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetMainDevice() )
                               ->GetVulkanLogicalDevice();

        m_ColorAttachments.clear();

        // Clear external attachments
        m_ExternalColorAttachments.clear();
        m_ExternalDepthAttachment.reset();

        if ( m_Framebuffer != VK_NULL_HANDLE )
        {
            vkDestroyFramebuffer( device, m_Framebuffer, nullptr );
            m_Framebuffer = VK_NULL_HANDLE;
        }

        if ( m_RenderPass != VK_NULL_HANDLE )
        {
            vkDestroyRenderPass( device, m_RenderPass, nullptr );
            m_RenderPass = VK_NULL_HANDLE;
        }
    }

    void VulkanFramebuffer::TransitionImagesToInitialLayouts()
    {
        auto commandBuffer =
             CommandBufferAllocator::GetInstance().RT_AllocateCommandBufferGraphic( true ).GetValue();

        for ( auto& colorAttachment : m_ColorAttachments )
        {
            auto    vulkanImage = sp_cast<VulkanImage2D>( colorAttachment );
            VkImage image       = vulkanImage->GetVulkanImageInfo().Image;

            VkImageMemoryBarrier barrier{};
            barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            barrier.image                           = image;
            barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel   = 0;
            barrier.subresourceRange.levelCount     = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount     = 1;
            barrier.srcAccessMask                   = 0;
            barrier.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier );
        }

        if ( m_DepthAttachment )
        {
            auto    vulkanImage = sp_cast<VulkanImage2D>( m_DepthAttachment );
            VkImage image       = vulkanImage->GetVulkanImageInfo().Image;

            VkImageMemoryBarrier barrier{};
            barrier.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout                   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
            barrier.image                       = image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            if ( Graphic::Utils::HasStencilComponent( m_DepthAttachment->GetImageSpecification().Format ) )
            {
                barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }
            barrier.subresourceRange.baseMipLevel   = 0;
            barrier.subresourceRange.levelCount     = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount     = 1;
            barrier.srcAccessMask                   = 0;
            barrier.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier );
        }

        CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( commandBuffer );
    }

} // namespace Desert::Graphic::API::Vulkan
