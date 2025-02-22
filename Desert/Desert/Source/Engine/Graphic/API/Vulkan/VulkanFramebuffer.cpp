#include <Engine/Graphic/API/Vulkan/VulkanFramebuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Graphic/Renderer.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>
#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Core/EngineContext.h>

namespace Desert::Graphic::API::Vulkan
{
    namespace
    {
        Common::Result<std::vector<VkFramebuffer>> CreateFramebuffers( VkDevice                         device,
                                                                       std::shared_ptr<VulkanSwapChain> swapChain,
                                                                       uint32_t width, uint32_t height )
        {
            std::vector<VkFramebuffer> frameBuffers;
            frameBuffers.resize( EngineContext::GetInstance().GetFramesInFlight() );

            /*uint32_t width  = EngineContext::GetInstance().GetCurrentWindowWidth();
            uint32_t height = EngineContext::GetInstance().GetCurrentWindowHeight();*/

            VkResult res;

           /* VkImageMemoryBarrier barrier            = {};
            barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout                       = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel   = 0;
            barrier.subresourceRange.levelCount     = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount     = 1;*/

            const auto& images = std::static_pointer_cast<Graphic::API::Vulkan::VulkanContext>(
                                      Renderer::GetInstance().GetRendererContext() )
                                      ->GetVulkanSwapChain()
                                      ->GetSwapChainVKImage();

            for ( uint32_t i = 0; i < frameBuffers.size(); i++ )
            {
                //barrier.image = images[i];

               /* auto copyCmd = CommandBufferAllocator::GetInstance().RT_AllocateCommandBufferGraphic( true );
                if ( !copyCmd.IsSuccess() )
                {
                    return Common::MakeError<std::vector<VkFramebuffer>>( "TODO: make error info" );
                }

                vkCmdPipelineBarrier( copyCmd.GetValue(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                      &barrier );
                CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( copyCmd.GetValue() );*/

                std::array<VkImageView, 2> attachments = { swapChain->GetSwapChainVKImagesView()[i],swapChain->GetDepthImages().ImageView };

                VkFramebufferCreateInfo fbCreateInfo = {};
                fbCreateInfo.sType                   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                fbCreateInfo.renderPass              = swapChain->GetRenderPass();
                fbCreateInfo.attachmentCount         = static_cast<uint32_t>( attachments.size() );
                fbCreateInfo.pAttachments            = attachments.data();
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
        if ( m_Framebuffers.empty() )
        {
            Release();
        }

        m_Width  = width;
        m_Height = height;

        const auto swapchain = std::static_pointer_cast<Graphic::API::Vulkan::VulkanContext>(
                                    Renderer::GetInstance().GetRendererContext() )
                                    ->GetVulkanSwapChain();

        const auto& device = Common::Singleton<VulkanLogicalDevice>::GetInstance().GetVulkanLogicalDevice();

        const auto result = CreateFramebuffers( device, swapchain, width, height );
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

        for ( uint32_t i = 0; i < m_Framebuffers.size(); i++ )
        {
            vkDestroyFramebuffer( device, m_Framebuffers[i], VK_NULL_HANDLE );
        }

        m_Framebuffers.clear();
    }

} // namespace Desert::Graphic::API::Vulkan