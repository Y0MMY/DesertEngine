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

        Common::Result<VkRenderPass>
        CreateRenderPass( VkDevice device, const std::vector<Core::Formats::ImageFormat>& colorAttachments,
                          const std::optional<Core::Formats::ImageFormat>& depthAttachment,
                          const std::vector<std::shared_ptr<Framebuffer>>& externalColorAttachments,
                          const std::optional<std::weak_ptr<Image2D>>&     externalDepthAttachment,
                          AttachmentLoad                                   defaultLoadOp )
        {
            std::vector<VkAttachmentDescription> attachmentDescriptions;
            std::vector<VkAttachmentReference>   colorAttachmentRefs;
            std::optional<VkAttachmentReference> depthAttachmentRef;

            // Process color attachments first
            for ( size_t i = 0; i < colorAttachments.size(); ++i )
            {
                const auto& format = colorAttachments[i];

                VkAttachmentDescription desc{};
                desc.format         = GetImageVulkanFormat( format );
                desc.samples        = VK_SAMPLE_COUNT_1_BIT;
                desc.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
                desc.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
                desc.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                desc.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
                desc.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                VkAttachmentReference ref{};
                ref.attachment = static_cast<uint32_t>( attachmentDescriptions.size() );
                ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                attachmentDescriptions.push_back( desc );
                colorAttachmentRefs.push_back( ref );
            }

            // Process external color attachments
            for ( const auto& externalFb : externalColorAttachments )
            {
                const auto& extAttachments = externalFb->GetSpecification().Attachments.Attachments;
                for ( const auto& format : extAttachments )
                {
                    if ( Graphic::Utils::IsDepthFormat( format ) )
                        continue;

                    VkAttachmentDescription desc{};
                    desc.format         = GetImageVulkanFormat( format );
                    desc.samples        = VK_SAMPLE_COUNT_1_BIT;
                    desc.loadOp         = GetVkAttachmentLoadOp( defaultLoadOp );
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
                }
            }

            // Process depth attachment (if exists)
            if ( depthAttachment )
            {
                VkAttachmentDescription desc{};
                desc.format         = GetImageVulkanFormat( *depthAttachment );
                desc.samples        = VK_SAMPLE_COUNT_1_BIT;
                desc.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
                desc.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
                desc.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                desc.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
                desc.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

                VkAttachmentReference ref{};
                ref.attachment = static_cast<uint32_t>( attachmentDescriptions.size() );
                ref.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

                attachmentDescriptions.push_back( desc );
                depthAttachmentRef = ref;
            }

            // Process external depth attachment (if exists)
            if ( externalDepthAttachment && !externalDepthAttachment->expired() )
            {
                auto depthImage = externalDepthAttachment->lock();
                auto format     = depthImage->GetImageSpecification().Format;

                VkAttachmentDescription desc{};
                desc.format         = GetImageVulkanFormat( format );
                desc.samples        = VK_SAMPLE_COUNT_1_BIT;
                desc.loadOp         = GetVkAttachmentLoadOp( defaultLoadOp );
                desc.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
                desc.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                desc.initialLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                desc.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

                VkAttachmentReference ref{};
                ref.attachment = static_cast<uint32_t>( attachmentDescriptions.size() );
                ref.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

                attachmentDescriptions.push_back( desc );
                depthAttachmentRef = ref;
            }

            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount    = static_cast<uint32_t>( colorAttachmentRefs.size() );
            subpass.pColorAttachments       = colorAttachmentRefs.data();
            subpass.pDepthStencilAttachment = depthAttachmentRef ? &( *depthAttachmentRef ) : nullptr;

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
        for ( const auto& externalFb : spec.ExternalAttachments.ExternalAttachments )
        {
            const auto& extAttachments = externalFb->GetSpecification().Attachments.Attachments;
            for ( size_t i = 0; i < extAttachments.size(); ++i )
            {
                const auto& format = extAttachments[i];
                if ( Graphic::Utils::IsDepthFormat( format ) )
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

            const auto renderPassResult = CreateRenderPass(
                 device, colorFormats, depthFormat,
                 m_FramebufferSpecification.ExternalAttachments.ExternalAttachments, m_ExternalDepthAttachment,
                 m_FramebufferSpecification.ExternalAttachments.Load );

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
            if ( const auto& attachment = externalColorAttachment.lock() )
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
        if ( const auto& externalDepthAttachment = m_ExternalDepthAttachment.lock() )
        {
            attachments.push_back(
                 sp_cast<VulkanImage2D>( externalDepthAttachment )->GetVulkanImageInfo().ImageInfo.imageView );
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

    Common::BoolResult VulkanFramebuffer::Release()
    {
        const auto& device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

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

        const auto& device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

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
} // namespace Desert::Graphic::API::Vulkan
