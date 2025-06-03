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
        Common::Result<VkRenderPass> CreateRenderPass( VkDevice                                       device,
                                                       const std::vector<Core::Formats::ImageFormat>& attachments )
        {
            std::vector<VkAttachmentDescription> attachmentDescriptions;
            std::vector<VkAttachmentReference>   attachmentRefs;

            for ( const auto& attachment : attachments )
            {
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
                    attachmentDescription.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                }
                else
                {
                    attachmentDescription.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
                    attachmentDescription.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
                    attachmentDescription.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                    attachmentDescription.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
                    attachmentDescription.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                }

                attachmentDescriptions.push_back( attachmentDescription );
            }

            for ( size_t i = 0; i < attachmentDescriptions.size(); ++i )
            {
                VkAttachmentReference attachmentRef = {};
                attachmentRef.attachment            = static_cast<uint32_t>( i );
                attachmentRef.layout                = ( Graphic::Utils::IsDepthFormat( attachments[i] ) )
                                                           ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                                           : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                attachmentRefs.push_back( attachmentRef );
            }

            VkSubpassDescription subpass = {};
            subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = attachmentRefs.size();
            subpass.pColorAttachments    = attachmentRefs.data();

            std::vector<VkSubpassDependency> dependencies( 2 );

            dependencies[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
            dependencies[0].dstSubpass      = 0;
            dependencies[0].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[0].srcAccessMask   = 0;
            dependencies[0].dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[0].dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

            dependencies[1].srcSubpass      = 0;
            dependencies[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
            dependencies[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dependencies[1].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT;
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
                return Common::MakeError<VkRenderPass>( "" );
            }

            return Common::MakeSuccess( renderPass );
        }
    } // namespace

    VulkanFramebuffer::VulkanFramebuffer( const FramebufferSpecification& spec )
         : m_FramebufferSpecification( spec )
    {

        for ( const auto attachment : spec.Attachments.Attachments )
        {
            Core::Formats::Image2DSpecification spec = { .Format     = attachment,
                                                         .Usage      = Core::Formats::Image2DUsage::Attachment,
                                                         .Properties = Core::Formats::Sample };

            if ( Graphic::Utils::IsDepthFormat( attachment ) )
            {
                m_DepthAttachment = std::make_shared<VulkanImage2D>( spec );
            }
            else
            {
                m_ColorAttachment = std::make_shared<VulkanImage2D>( spec );
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

        if ( m_DepthAttachment )
        {
            auto& spec = m_DepthAttachment->GetImageSpecification();

            spec.Width  = width;
            spec.Height = height;
            sp_cast<VulkanImage2D>( m_DepthAttachment )->RT_Invalidate();
        }

        if ( m_ColorAttachment )
        {
            auto& spec = m_ColorAttachment->GetImageSpecification();

            spec.Width  = width;
            spec.Height = height;
            sp_cast<VulkanImage2D>( m_ColorAttachment )->RT_Invalidate();
        }

        const auto& device = Common::Singleton<VulkanLogicalDevice>::GetInstance().GetVulkanLogicalDevice();

        const auto result = CreateFramebuffer( device, width, height );
        if ( !result.IsSuccess() )
        {
            return Common::MakeError( result.GetError() );
        }

        return Common::MakeSuccess( true );
    }

    void VulkanFramebuffer::Use( BindUsage /*= BindUsage::Bind */ ) const
    {
    }

    Common::Result<VkFramebuffer> VulkanFramebuffer::CreateFramebuffer( VkDevice device, uint32_t width,
                                                                        uint32_t height )
    {
        VkFramebuffer frameBuffer;

        const auto renderPassResult =
             CreateRenderPass( device, m_FramebufferSpecification.Attachments.Attachments );
        if ( !renderPassResult.IsSuccess() )
        {
            return Common::MakeError<VkFramebuffer>( renderPassResult.GetError() );
        }
        const auto renderPass = renderPassResult.GetValue();

        m_RenderPass = renderPass;

        std::vector<VkImageView> attachments( 1u );
        attachments[0] = sp_cast<VulkanImage2D>( m_ColorAttachment )->GetVulkanImageInfo().ImageView;

        VkFramebufferCreateInfo framebufferCreateInfo = {};
        framebufferCreateInfo.sType                   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferCreateInfo.renderPass              = m_RenderPass;
        framebufferCreateInfo.attachmentCount         = static_cast<uint32_t>( attachments.size() );
        framebufferCreateInfo.pAttachments            = attachments.data();
        framebufferCreateInfo.width                   = width;
        framebufferCreateInfo.height                  = height;
        framebufferCreateInfo.layers                  = 1;

        auto res = vkCreateFramebuffer( device, &framebufferCreateInfo, nullptr, &m_Framebuffer );
        VKUtils::SetDebugUtilsObjectName(
             device, VK_OBJECT_TYPE_FRAMEBUFFER,
             ( "FB Color Attachmanet (" + m_FramebufferSpecification.DebugName + ")" ), m_Framebuffer );
        return Common::MakeSuccess( frameBuffer );
    }

    Common::BoolResult VulkanFramebuffer::Release()
    {
        const auto& device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

        m_ColorAttachment->Release();
        // m_DepthAttachment->Release();

        vkDestroyFramebuffer( device, m_Framebuffer, VK_NULL_HANDLE );
        vkDestroyRenderPass( device, m_RenderPass, VK_NULL_HANDLE );

        m_Framebuffer = VK_NULL_HANDLE;

        return Common::MakeSuccess( true );
    }

} // namespace Desert::Graphic::API::Vulkan