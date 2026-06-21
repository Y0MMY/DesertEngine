#include <Engine/Graphic/API/Vulkan/VulkanFramebuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>

#include <Engine/Core/EngineContext.hpp>

namespace Desert::Graphic::API::Vulkan
{
    VulkanFramebuffer::VulkanFramebuffer( const FramebufferSpecification& spec ) 
        : m_FramebufferSpecification( spec ), m_Width( spec.Width ), m_Height( spec.Height )
    {
    }

    VulkanFramebuffer::~VulkanFramebuffer()
    {
        Release();
    }

    Common::BoolResultStr VulkanFramebuffer::RT_Invalidate()
    {
        Release();

        auto vkDevice = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetDevice() )->GetVulkanLogicalDevice();

        std::vector<VkAttachmentDescription> attachmentDescriptions;
        std::vector<VkAttachmentReference>   colorAttachmentReferences;
        VkAttachmentReference                depthAttachmentReference = {};
        bool                                 hasDepth                 = false;

        auto toVkLoadOp = []( AttachmentLoad op ) -> VkAttachmentLoadOp
        {
            switch ( op )
            {
                case AttachmentLoad::Load:     return VK_ATTACHMENT_LOAD_OP_LOAD;
                case AttachmentLoad::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                default:                       return VK_ATTACHMENT_LOAD_OP_CLEAR;
            }
        };
        auto toVkStoreOp = []( AttachmentStore op ) -> VkAttachmentStoreOp
        {
            return op == AttachmentStore::DontCare ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                                   : VK_ATTACHMENT_STORE_OP_STORE;
        };

        uint32_t i = 0;
        for ( const auto& attachment : m_FramebufferSpecification.Attachments.Attachments )
        {
            const auto format = attachment.Format;
            if ( Graphic::Utils::IsDepthFormat( format ) )
            {
                VkAttachmentDescription& depthAttachment = attachmentDescriptions.emplace_back();
                depthAttachment.format         = GetImageVulkanFormat( format );
                depthAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
                depthAttachment.loadOp         = toVkLoadOp( attachment.LoadOp );
                depthAttachment.storeOp        = toVkStoreOp( attachment.StoreOp );
                depthAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                depthAttachment.initialLayout  = attachment.LoadOp == AttachmentLoad::Load
                                                     ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                                     : VK_IMAGE_LAYOUT_UNDEFINED;
                depthAttachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

                depthAttachmentReference.attachment = i;
                depthAttachmentReference.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                hasDepth                            = true;
            }
            else
            {
                VkAttachmentDescription& colorAttachment = attachmentDescriptions.emplace_back();
                colorAttachment.format         = GetImageVulkanFormat( format );
                colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
                colorAttachment.loadOp         = toVkLoadOp( attachment.LoadOp );
                colorAttachment.storeOp        = toVkStoreOp( attachment.StoreOp );
                colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                colorAttachment.initialLayout  = attachment.LoadOp == AttachmentLoad::Load
                                                     ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                     : VK_IMAGE_LAYOUT_UNDEFINED;
                colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                colorAttachmentReferences.push_back( { i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } );
            }
            i++;
        }

        VkSubpassDescription subpassDescription = {};
        subpassDescription.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription.colorAttachmentCount = static_cast<uint32_t>( colorAttachmentReferences.size() );
        subpassDescription.pColorAttachments    = colorAttachmentReferences.data();
        subpassDescription.pDepthStencilAttachment = hasDepth ? &depthAttachmentReference : nullptr;

        std::vector<VkSubpassDependency> dependencies;
        if ( !colorAttachmentReferences.empty() )
        {
            VkSubpassDependency& dependency = dependencies.emplace_back();
            dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
            dependency.dstSubpass    = 0;
            dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.srcAccessMask = 0;
            dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        }

        if ( hasDepth )
        {
            VkSubpassDependency& dependency = dependencies.emplace_back();
            dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
            dependency.dstSubpass    = 0;
            dependency.srcStageMask  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            dependency.srcAccessMask = 0;
            dependency.dstStageMask  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }

        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>( attachmentDescriptions.size() );
        renderPassInfo.pAttachments    = attachmentDescriptions.data();
        renderPassInfo.subpassCount    = 1;
        renderPassInfo.pSubpasses      = &subpassDescription;
        renderPassInfo.dependencyCount = static_cast<uint32_t>( dependencies.size() );
        renderPassInfo.pDependencies    = dependencies.data();

        VK_CHECK_RESULT( vkCreateRenderPass( vkDevice, &renderPassInfo, nullptr, &m_RenderPass ) );

        std::vector<VkImageView> attachments;
        for ( const auto& attachment : m_FramebufferSpecification.Attachments.Attachments )
        {
            const auto format = attachment.Format;
            Core::Formats::Image2DSpecification imageSpec = {
                 .Tag        = m_FramebufferSpecification.DebugName + "_attachment",
                 .Width      = m_FramebufferSpecification.Width,
                 .Height     = m_FramebufferSpecification.Height,
                 .Format     = format,
                 .Mips       = 1,
                 .Usage      = Core::Formats::Image2DUsage::Attachment,
                 .Properties = Core::Formats::Sample };

            auto image = std::make_shared<VulkanImage2D>( imageSpec );
            image->RT_Invalidate();

            if ( Graphic::Utils::IsDepthFormat( format ) )
                m_DepthAttachment = image;
            else
                m_ColorAttachments.push_back( image );

            attachments.push_back( image->GetResource().ImageView );
        }

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass      = m_RenderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>( attachments.size() );
        framebufferInfo.pAttachments    = attachments.data();
        framebufferInfo.width           = m_FramebufferSpecification.Width;
        framebufferInfo.height          = m_FramebufferSpecification.Height;
        framebufferInfo.layers          = 1;

        VK_CHECK_RESULT( vkCreateFramebuffer( vkDevice, &framebufferInfo, nullptr, &m_Framebuffer ) );

        m_Width = m_FramebufferSpecification.Width;
        m_Height = m_FramebufferSpecification.Height;

        return BOOLSUCCESS;
    }

    Common::BoolResultStr VulkanFramebuffer::Invalidate()
    {
        return RT_Invalidate();
    }

    Common::BoolResultStr VulkanFramebuffer::Release()
    {
        auto allocator = SP_CAST( VulkanContext, EngineContext::GetInstance().GetRendererContext() )->GetVulkanAllocator().get();

        if ( m_Framebuffer ) allocator->RT_DestroyFramebuffer( m_Framebuffer );
        if ( m_RenderPass )  allocator->RT_DestroyRenderPass( m_RenderPass );

        m_Framebuffer = VK_NULL_HANDLE;
        m_RenderPass  = VK_NULL_HANDLE;
        m_ColorAttachments.clear();
        m_DepthAttachment = nullptr;

        return BOOLSUCCESS;
    }

    Common::BoolResultStr VulkanFramebuffer::Resize( uint32_t width, uint32_t height, bool forceRecreate )
    {
        m_FramebufferSpecification.Width = width;
        m_FramebufferSpecification.Height = height;
        return RT_Invalidate();
    }

} // namespace Desert::Graphic::API::Vulkan
