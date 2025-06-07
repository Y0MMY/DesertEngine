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

        Common::Result<VkRenderPass> CreateRenderPass( VkDevice                                       device,
                                                       const std::vector<Core::Formats::ImageFormat>& attachments,
                                                       const ExternalFramebuffer& externalAttachments,
                                                       AttachmentLoad             defaultLoadOp )
        {
            std::vector<VkAttachmentDescription> attachmentDescriptions;
            std::vector<VkAttachmentReference>   colorAttachmentRefs;
            VkAttachmentReference                depthAttachmentRef = {};

            bool     hasDepthAttachment = false;
            uint32_t attachmentIndex    = 0;

            // Process main attachments
            for ( size_t i = 0; i < attachments.size(); ++i )
            {
                const auto&             attachment            = attachments[i];
                VkAttachmentDescription attachmentDescription = {};
                attachmentDescription.format                  = GetImageVulkanFormat( attachment );
                attachmentDescription.samples                 = VK_SAMPLE_COUNT_1_BIT;

                if ( Graphic::Utils::IsDepthFormat( attachment ) )
                {
                    attachmentDescription.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
                    attachmentDescription.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
                    attachmentDescription.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                    attachmentDescription.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
                    attachmentDescription.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

                    depthAttachmentRef.attachment = static_cast<uint32_t>( attachmentIndex++ );
                    depthAttachmentRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    hasDepthAttachment            = true;
                }
                else
                {
                    attachmentDescription.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
                    attachmentDescription.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
                    attachmentDescription.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                    attachmentDescription.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
                    attachmentDescription.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                    VkAttachmentReference colorAttachmentRef = {};
                    colorAttachmentRef.attachment            = static_cast<uint32_t>( attachmentIndex++ );
                    colorAttachmentRef.layout                = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    colorAttachmentRefs.push_back( colorAttachmentRef );
                }

                attachmentDescriptions.push_back( attachmentDescription );
            }

            // Process external attachments
            for ( const auto& externalFb : externalAttachments.ExternalAttachments )
            {
                const auto& extAttachments = externalFb->GetSpecification().Attachments.Attachments;
                for ( size_t i = 0; i < extAttachments.size(); ++i )
                {
                    const auto&             attachment            = extAttachments[i];
                    VkAttachmentDescription attachmentDescription = {};
                    attachmentDescription.format                  = GetImageVulkanFormat( attachment );
                    attachmentDescription.samples                 = VK_SAMPLE_COUNT_1_BIT;

                    if ( Graphic::Utils::IsDepthFormat( attachment ) )
                    {
                        attachmentDescription.loadOp         = GetVkAttachmentLoadOp( externalAttachments.Load );
                        attachmentDescription.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
                        attachmentDescription.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                        attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                        attachmentDescription.initialLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        attachmentDescription.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

                        depthAttachmentRef.attachment = static_cast<uint32_t>( attachmentIndex++ );
                        depthAttachmentRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                        hasDepthAttachment            = true;
                    }
                    else
                    {
                        attachmentDescription.loadOp         = GetVkAttachmentLoadOp( externalAttachments.Load );
                        attachmentDescription.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
                        attachmentDescription.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                        attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                        attachmentDescription.initialLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        attachmentDescription.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                        VkAttachmentReference colorAttachmentRef = {};
                        colorAttachmentRef.attachment            = static_cast<uint32_t>( attachmentIndex++ );
                        colorAttachmentRef.layout                = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                        colorAttachmentRefs.push_back( colorAttachmentRef );
                    }

                    attachmentDescriptions.push_back( attachmentDescription );
                }
            }

            VkSubpassDescription subpass    = {};
            subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount    = static_cast<uint32_t>( colorAttachmentRefs.size() );
            subpass.pColorAttachments       = colorAttachmentRefs.data();
            subpass.pDepthStencilAttachment = hasDepthAttachment ? &depthAttachmentRef : nullptr;

            std::vector<VkSubpassDependency> dependencies( 2 );

            dependencies[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
            dependencies[0].dstSubpass    = 0;
            dependencies[0].srcStageMask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            dependencies[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            dependencies[0].dstAccessMask =
                 VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

            dependencies[1].srcSubpass      = 0;
            dependencies[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
            dependencies[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[1].dstStageMask    = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            dependencies[1].dstAccessMask   = VK_ACCESS_MEMORY_READ_BIT;
            dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

            VkRenderPassCreateInfo renderPassCreateInfo = {};
            renderPassCreateInfo.sType                  = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassCreateInfo.attachmentCount        = static_cast<uint32_t>( attachmentDescriptions.size() );
            renderPassCreateInfo.pAttachments           = attachmentDescriptions.data();
            renderPassCreateInfo.subpassCount           = 1;
            renderPassCreateInfo.pSubpasses             = &subpass;
            renderPassCreateInfo.dependencyCount        = static_cast<uint32_t>( dependencies.size() );
            renderPassCreateInfo.pDependencies          = dependencies.data();

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
        // Create our own attachments
        for ( const auto attachment : spec.Attachments.Attachments )
        {
            Core::Formats::Image2DSpecification imageSpec = { .Format = attachment,
                                                              .Usage  = Core::Formats::Image2DUsage::Attachment,
                                                              .Properties = Core::Formats::Sample };

            if ( Graphic::Utils::IsDepthFormat( attachment ) )
            {
                m_DepthAttachment = std::make_shared<VulkanImage2D>( imageSpec );
            }
            else
            {
                m_ColorAttachments.push_back( std::make_shared<VulkanImage2D>( imageSpec ) );
            }
        }

        // Store references to external attachments
        for ( const auto& externalFb : spec.ExternalAttachments.ExternalAttachments )
        {
            for ( size_t i = 0; i < externalFb->GetSpecification().Attachments.Attachments.size(); ++i )
            {
                const auto& attachment = externalFb->GetSpecification().Attachments.Attachments[i];
                if ( Graphic::Utils::IsDepthFormat( attachment ) )
                {
                    m_ExternalDepthAttachment = externalFb->GetDepthAttachmentImage();
                }
                else
                {
                    m_ExternalColorAttachments.push_back( externalFb->GetColorAttachmentImage( i ) );
                }
            }
        }
    }

    Common::BoolResult VulkanFramebuffer::Resize( uint32_t width, uint32_t height, bool forceRecreate )
    {
        if ( m_Framebuffer != VK_NULL_HANDLE )
        {
            Release();
        }

        m_Width  = width;
        m_Height = height;

        // Resize our own attachments
        if ( m_DepthAttachment )
        {
            auto& spec  = m_DepthAttachment->GetImageSpecification();
            spec.Width  = width;
            spec.Height = height;
            sp_cast<VulkanImage2D>( m_DepthAttachment )->RT_Invalidate();
        }

        for ( auto& colorAttachment : m_ColorAttachments )
        {
            auto& spec  = colorAttachment->GetImageSpecification();
            spec.Width  = width;
            spec.Height = height;
            sp_cast<VulkanImage2D>( colorAttachment )->RT_Invalidate();
        }

        const auto& device = Common::Singleton<VulkanLogicalDevice>::GetInstance().GetVulkanLogicalDevice();
        const auto  result = CreateFramebuffer( device, width, height );
        if ( !result.IsSuccess() )
        {
            return Common::MakeError( result.GetError() );
        }

        return Common::MakeSuccess( true );
    }

    Common::Result<VkFramebuffer> VulkanFramebuffer::CreateFramebuffer( VkDevice device, uint32_t width,
                                                                        uint32_t height )
    {
        if ( m_RenderPass == nullptr )
        {
            const auto renderPassResult =
                 CreateRenderPass( device, m_FramebufferSpecification.Attachments.Attachments,
                                   m_FramebufferSpecification.ExternalAttachments,
                                   m_FramebufferSpecification.ExternalAttachments.Load );
            if ( !renderPassResult.IsSuccess() )
            {
                return Common::MakeError<VkFramebuffer>( renderPassResult.GetError() );
            }
            m_RenderPass = renderPassResult.GetValue();
        }

        std::vector<VkImageView> attachments;

        for ( const auto& colorAttachment : m_ColorAttachments )
        {
            attachments.push_back( sp_cast<VulkanImage2D>( colorAttachment )->GetVulkanImageInfo().ImageView );
        }

        if ( m_DepthAttachment )
        {
            attachments.push_back( sp_cast<VulkanImage2D>( m_DepthAttachment )->GetVulkanImageInfo().ImageView );
        }

        for ( const auto& externalColorAttachment : m_ExternalColorAttachments )
        {
            attachments.push_back(
                 sp_cast<VulkanImage2D>( externalColorAttachment )->GetVulkanImageInfo().ImageView );
        }

        if ( m_ExternalDepthAttachment )
        {
            attachments.push_back(
                 sp_cast<VulkanImage2D>( m_ExternalDepthAttachment )->GetVulkanImageInfo().ImageView );
        }

        VkFramebufferCreateInfo framebufferCreateInfo = {};
        framebufferCreateInfo.sType                   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferCreateInfo.renderPass              = m_RenderPass;
        framebufferCreateInfo.attachmentCount         = static_cast<uint32_t>( attachments.size() );
        framebufferCreateInfo.pAttachments            = attachments.data();
        framebufferCreateInfo.width                   = width;
        framebufferCreateInfo.height                  = height;
        framebufferCreateInfo.layers                  = 1;

        if ( vkCreateFramebuffer( device, &framebufferCreateInfo, nullptr, &m_Framebuffer ) != VK_SUCCESS )
        {
            return Common::MakeError<VkFramebuffer>( "Failed to create framebuffer" );
        }

        VKUtils::SetDebugUtilsObjectName( device, VK_OBJECT_TYPE_FRAMEBUFFER,
                                          ( "FB (" + m_FramebufferSpecification.DebugName + ")" ), m_Framebuffer );

        return Common::MakeSuccess( m_Framebuffer );
    }

    Common::BoolResult VulkanFramebuffer::Release()
    {
        const auto& device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

        for ( auto& colorAttachment : m_ColorAttachments )
        {
            colorAttachment->Release();
            colorAttachment.reset();
        }

        if ( m_DepthAttachment )
        {
            m_DepthAttachment->Release();
            m_DepthAttachment.reset();
        }

        if ( m_Framebuffer != VK_NULL_HANDLE )
        {
            vkDestroyFramebuffer( device, m_Framebuffer, nullptr );
            m_Framebuffer = VK_NULL_HANDLE;
        }

        return Common::MakeSuccess( true );
    }
} // namespace Desert::Graphic::API::Vulkan