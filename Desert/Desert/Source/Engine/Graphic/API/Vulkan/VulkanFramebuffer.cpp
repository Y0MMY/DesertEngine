#include <Engine/Graphic/API/Vulkan/VulkanFramebuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Graphic/Renderer.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>
#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Core/EngineContext.h>

namespace Desert::Graphic::API::Vulkan
{
    namespace Format
    {
        VkAttachmentDescription attachDesc = { .flags          = 0,
                                               .samples        = VK_SAMPLE_COUNT_1_BIT,
                                               .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                               .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
                                               .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                               .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                               .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
                                               .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };

        VkAttachmentReference attachRef = { .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpassDesc = { .flags                   = 0,
                                             .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
                                             .inputAttachmentCount    = 0,
                                             .pInputAttachments       = NULL,
                                             .colorAttachmentCount    = 1,
                                             .pColorAttachments       = &attachRef,
                                             .pResolveAttachments     = NULL,
                                             .pDepthStencilAttachment = NULL,
                                             .preserveAttachmentCount = 0,
                                             .pPreserveAttachments    = NULL };

        VkRenderPassCreateInfo renderPassCreateInfo = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                                        .pNext = NULL,
                                                        .flags = 0,
                                                        .attachmentCount = 1,
                                                        .pAttachments    = &attachDesc,
                                                        .subpassCount    = 1,
                                                        .pSubpasses      = &subpassDesc,
                                                        .dependencyCount = 0,
                                                        .pDependencies   = NULL };

    } // namespace Format

    namespace
    {
        Common::Result<std::vector<VkFramebuffer>> CreateFramebuffers( VkDevice device, VkRenderPass renderPass,
                                                                       std::vector<VkImageView> imagesView,
                                                                       uint32_t width, uint32_t height )
        {
            std::vector<VkFramebuffer> frameBuffers;
            frameBuffers.resize( EngineContext::GetInstance().GetFramesInFlight() );

            /*uint32_t width  = EngineContext::GetInstance().GetCurrentWindowWidth();
            uint32_t height = EngineContext::GetInstance().GetCurrentWindowHeight();*/

            VkResult res;

            VkImageMemoryBarrier barrier            = {};
            barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout                       = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel   = 0;
            barrier.subresourceRange.levelCount     = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount     = 1;

            for ( uint32_t i = 0; i < frameBuffers.size(); i++ )
            {
                const auto& images = std::static_pointer_cast<Graphic::API::Vulkan::VulkanContext>(
                                          Renderer::GetInstance().GetRendererContext() )
                                          ->GetVulkanSwapChain()
                                          ->GetVKImage();

                barrier.image = images[i];

                auto copyCmd = CommandBufferAllocator::GetInstance().RT_GetCommandBufferGraphic( true );
                if ( !copyCmd.IsSuccess() )
                {
                    return Common::MakeError<std::vector<VkFramebuffer>>( "TODO: make error info" );
                }

                vkCmdPipelineBarrier( copyCmd.GetValue(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                      &barrier );
                CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( copyCmd.GetValue() );

                VkFramebufferCreateInfo fbCreateInfo = {};
                fbCreateInfo.sType                   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                fbCreateInfo.renderPass              = renderPass;
                fbCreateInfo.attachmentCount         = 1;
                fbCreateInfo.pAttachments            = &imagesView[i];
                fbCreateInfo.width                   = width;
                fbCreateInfo.height                  = height;
                fbCreateInfo.layers                  = 1;

                res = vkCreateFramebuffer( device, &fbCreateInfo, NULL, &frameBuffers[i] );
                if ( res != VK_SUCCESS )
                {
                    return Common::MakeError<std::vector<VkFramebuffer>>( "TODO: make error info" );
                }
            }
            return Common::MakeSuccess( frameBuffers );
        }
    } // namespace

    VulkanFramebuffer::VulkanFramebuffer( const FramebufferSpecification& spec )
         : m_FramebufferSpecification( spec )
    {
    }

    Common::BoolResult VulkanFramebuffer::Resize( uint32_t width, uint32_t height, bool forceRecreate )
    {
        if ( m_VkRenderPass != VK_NULL_HANDLE )
        {
            Release();
        }

        m_Width  = width;
        m_Height = height;

        VkFormat format = std::static_pointer_cast<Graphic::API::Vulkan::VulkanContext>(
                               Renderer::GetInstance().GetRendererContext() )
                               ->GetVulkanSwapChain()
                               ->GetColorFormat();

        const auto imagesView = std::static_pointer_cast<Graphic::API::Vulkan::VulkanContext>(
                                     Renderer::GetInstance().GetRendererContext() )
                                     ->GetVulkanSwapChain()
                                     ->GetVKImagesView();

        Format::attachDesc.format = format;

        const auto& device = Common::Singleton<VulkanLogicalDevice>::GetInstance().GetVulkanLogicalDevice();
        VK_RETURN_RESULT_IF_FALSE_TYPE(
             bool, vkCreateRenderPass( device, &Format::renderPassCreateInfo, VK_NULL_HANDLE, &m_VkRenderPass ) );

        const auto result = CreateFramebuffers( device, m_VkRenderPass, imagesView, width, height );
        if ( !result.IsSuccess() )
        {
            return Common::MakeError<bool>( result.GetError() );
        }

        m_Framebuffers = result.GetValue();
    }

    void VulkanFramebuffer::Use( BindUsage /*= BindUsage::Bind */ ) const
    {
    }

    void VulkanFramebuffer::Release()
    {
        const auto& device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

        vkDestroyRenderPass( device, m_VkRenderPass, VK_NULL_HANDLE );
        m_VkRenderPass = VK_NULL_HANDLE;

        for ( uint32_t i = 0; i < m_Framebuffers.size(); i++ )
        {
            vkDestroyFramebuffer( device, m_Framebuffers[i], VK_NULL_HANDLE );
            m_Framebuffers[i] = VK_NULL_HANDLE;
        }
    }

} // namespace Desert::Graphic::API::Vulkan