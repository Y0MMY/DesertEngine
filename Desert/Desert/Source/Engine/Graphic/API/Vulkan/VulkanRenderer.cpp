#include <Engine/Graphic/API/Vulkan/VulkanRenderer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderCommandBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanFramebuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanPipeline.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanVertexBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanIndexBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanMaterialBackend.hpp>
#include <Engine/Graphic/ComputeImages.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>

#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanSwapChain.hpp>

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/Texture.hpp>

#include <Engine/Core/EngineContext.hpp>
#include "stb_image/stb_image_write.h"

#include <glm/glm.hpp>

namespace Desert::Graphic::API::Vulkan
{
    namespace
    {
        bool ShouldUseDynamicLineWidth( const std::shared_ptr<Pipeline>& pipeline )
        {
            const auto& spec = pipeline->GetSpecification();
            if ( spec.PolygonMode == PrimitivePolygonMode::Wireframe || Graphic::PrimitiveIsLine( spec.Topology ) )
            {
                return true;
            }

            return false;
        }
    } // namespace

    struct SpecularFilterPushConstants
    {
        uint32_t mipLevel;
        float    roughness;
    };

    Common::BoolResultStr VulkanRendererAPI::BeginFrame()
    {
        if ( m_CurrentCommandBuffer != nullptr )
        {
            return Common::MakeError<bool>( "BeginFrame(): Error! Have you call EndFrame() ?" );
        }

        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        VkCommandBufferBeginInfo cmdBufferBeginInfo{};
        cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        m_CurrentCommandBuffer = VulkanRenderCommandBuffer::GetInstance().GetCommandBuffer();
        vkBeginCommandBuffer( m_CurrentCommandBuffer, &cmdBufferBeginInfo );

        return Common::MakeSuccess( true );
    }

    Common::BoolResultStr VulkanRendererAPI::EndFrame()
    {
        if ( m_CurrentCommandBuffer == nullptr )
        {
            return Common::MakeError<bool>( "EndFrame(): Error! Have you call BeginFrame() ?" );
        }

        VulkanRenderCommandBuffer::GetInstance().ExecuteUserCommands();
        VkResult res = vkEndCommandBuffer( m_CurrentCommandBuffer );

        m_CurrentCommandBuffer = nullptr;

        return Common::MakeSuccess( true );
    }

    Common::BoolResultStr VulkanRendererAPI::PrepareNextFrame()
    {
        // static_cast<VulkanContext*>( Renderer::GetInstance().GetRendererContext().get() )->PrepareNextFrame();

        return Common::MakeSuccess( true );
    }

    Common::BoolResultStr VulkanRendererAPI::PresentFinalImage()
    {
        // static_cast<VulkanContext*>( Renderer::GetInstance().GetRendererContext().get() )->PresentFinalImage();

        return Common::MakeSuccess( true );
    }

    void VulkanRendererAPI::Init()
    {
        const auto window = m_Window.lock();
        if ( !window )
        {
            DESERT_VERIFY( false );
        }

        VulkanRenderCommandBuffer::CreateInstance( "Main" ).Init(
             SP_CAST( VulkanSwapChain, window->GetWindowSwapChain() )->GetVulkanQueue().get() );
    }

    std::shared_ptr<VulkanFramebuffer> GetFramebuffer( const std::shared_ptr<RenderPass>& renderPass )
    {
        return sp_cast<Graphic::API::Vulkan::VulkanFramebuffer>(
             renderPass->GetSpecification().TargetFramebuffer );
    }

    VulkanSwapChain* GetSwapChain( const std::shared_ptr<Window>& window )
    {
        return SP_CAST( VulkanSwapChain, window->GetWindowSwapChain() ).get();
    }

    VkRenderPassBeginInfo CreateRenderPassBeginInfo( VulkanSwapChain*                          swapChain,
                                                     const std::shared_ptr<VulkanFramebuffer>& framebuffer,
                                                     const std::vector<VkClearValue>&          clearValues )
    {
        const auto width  = framebuffer->GetFramebufferWidth();
        const auto height = framebuffer->GetFramebufferHeight();

        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType                 = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass            = framebuffer->GetVKRenderPass();
        renderPassBeginInfo.renderArea.offset     = { 0, 0 };
        renderPassBeginInfo.renderArea.extent     = { width, height };
        renderPassBeginInfo.clearValueCount       = clearValues.size();
        renderPassBeginInfo.pClearValues          = clearValues.data();
        renderPassBeginInfo.framebuffer           = framebuffer->GetVKFramebuffer();
        return renderPassBeginInfo;
    }

    VkRenderPassBeginInfo CreateRenderPassBeginInfo( VulkanSwapChain* swapChain, const VkFramebuffer framebuffer,
                                                     const std::array<VkClearValue, 2>& clearValues )
    {
        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType                 = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass            = swapChain->GetRenderPass();
        renderPassBeginInfo.renderArea.offset     = { 0, 0 };
        renderPassBeginInfo.renderArea.extent     = { swapChain->GetWidth(), swapChain->GetHeight() };
        renderPassBeginInfo.clearValueCount       = static_cast<uint32_t>( clearValues.size() );
        renderPassBeginInfo.pClearValues          = clearValues.data();
        renderPassBeginInfo.framebuffer           = framebuffer;
        return renderPassBeginInfo;
    }

    Common::BoolResultStr VulkanRendererAPI::BeginRenderPass( const std::shared_ptr<RenderPass>& renderPass,
                                                           bool                               clearFrame )
    {
        const auto window = m_Window.lock();
        if ( !window )
        {
            DESERT_VERIFY( false );
        }

        auto clearValues =
             std::static_pointer_cast<VulkanFramebuffer>( renderPass->GetSpecification().TargetFramebuffer )
                  ->GetClearValues();
        auto        framebuffer = GetFramebuffer( renderPass );
        const auto& swapChain   = GetSwapChain( window );

        m_CompositeFramebuffer = framebuffer;

        VkRenderPassBeginInfo renderPassBeginInfo =
             CreateRenderPassBeginInfo( swapChain, framebuffer, clearValues );
        vkCmdBeginRenderPass( m_CurrentCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE );

        if ( clearFrame )
        {
            ClearAttachments( clearValues, framebuffer );
        }
        SetViewportAndScissor( framebuffer->GetFramebufferWidth(), framebuffer->GetFramebufferHeight() );
        return Common::MakeSuccess( true );
    }

    Common::BoolResultStr VulkanRendererAPI::BeginSwapChainRenderPass()
    {
        const auto window = m_Window.lock();
        if ( !window )
        {
            DESERT_VERIFY( false );
        }

        uint32_t                    frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();
        std::array<VkClearValue, 2> clearValues{};
        const auto&                 swapChain   = GetSwapChain( window );
        const auto                  framebuffer = swapChain->GetVKFramebuffers();

        VkRenderPassBeginInfo renderPassBeginInfo =
             CreateRenderPassBeginInfo( swapChain, framebuffer[frameIndex], clearValues );
        vkCmdBeginRenderPass( m_CurrentCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE );

        SetViewportAndScissor( swapChain->GetWidth(), swapChain->GetHeight() );
        return Common::MakeSuccess( true );
    }

    Common::BoolResultStr VulkanRendererAPI::EndRenderPass()
    {
        vkCmdEndRenderPass( m_CurrentCommandBuffer );
        return Common::MakeSuccess( true );
    }

    void VulkanRendererAPI::ResizeWindowEvent( uint32_t width, uint32_t height )
    {
        /* if ( width == 0 && height == 0 )
             return;
         const auto device       = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();
         uint32_t   currentIndex = Renderer::GetInstance().GetCurrentFrameIndex();

         const auto& pool = CommandBufferAllocator::GetInstance().GetCommandGraphicPool();
         VK_CHECK_RESULT( vkResetCommandPool( device, pool[currentIndex], 0 ) );

         const auto& swapChain = static_cast<VulkanContext*>( Renderer::GetInstance().GetRendererContext().get() )
                                      ->GetVulkanSwapChain();
         if ( width == swapChain->GetWidth() && height == swapChain->GetHeight() )
             return;

         swapChain->OnResize( width, height );

         auto commandBuffer = CommandBufferAllocator::GetInstance().RT_AllocateCommandBufferGraphic( true );

         for ( auto& image : swapChain->GetSwapChainVKImage() )
         {
             VkImageMemoryBarrier barrier{};
             barrier.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
             barrier.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
             barrier.newLayout                   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
             barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
             barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
             barrier.image                       = image;
             barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
             barrier.subresourceRange.levelCount = 1;
             barrier.subresourceRange.layerCount = 1;

             vkCmdPipelineBarrier( commandBuffer.GetValue(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                   &barrier );
         }

         CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( commandBuffer.GetValue() );*/
    }

    void VulkanRendererAPI::SetViewportAndScissor( const uint32_t wdith, const uint32_t height )
    {
        VkViewport viewport = {};
        viewport.x          = 0.0f;
        viewport.y          = (float)height;
        viewport.height     = -(float)height;
        viewport.width      = (float)wdith;
        viewport.minDepth   = 0.0f;
        viewport.maxDepth   = 1.0f;

        VkRect2D scissor = {};
        scissor.offset   = { 0, 0 };
        scissor.extent   = { wdith, height };

        vkCmdSetViewport( m_CurrentCommandBuffer, 0, 1, &viewport );
        vkCmdSetScissor( m_CurrentCommandBuffer, 0, 1, &scissor );
    }

    std::shared_ptr<Framebuffer> VulkanRendererAPI::GetCompositeFramebuffer() const
    {
        if ( const auto& framebuffer = m_CompositeFramebuffer.lock() ) [[likely]]
        {
            return framebuffer;
        }
        return nullptr;
    }

    void VulkanRendererAPI::SubmitFullscreenQuad( const std::shared_ptr<Pipeline>&         pipeline,
                                                  const std::shared_ptr<MaterialExecutor>& material )
    {
        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();
        material->Apply();

        const auto& vulkanLayout = std::static_pointer_cast<Graphic::API::Vulkan::VulkanPipeline>( pipeline );
        static_cast<VulkanMaterialBackend*>( material->GetMaterialBackend().get() )
             ->BindDescriptorSets( m_CurrentCommandBuffer, vulkanLayout->GetVkPipelineLayout(),
                                   VK_PIPELINE_BIND_POINT_GRAPHICS, frameIndex );

        vkCmdBindPipeline( m_CurrentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           vulkanLayout->GetVkPipeline() );

        vkCmdDraw( m_CurrentCommandBuffer, 6, 1, 0, 0 );
    }

    void VulkanRendererAPI::RenderMesh( const std::shared_ptr<Pipeline>&         pipeline,
                                        const std::shared_ptr<Mesh>&             mesh,
                                        const std::shared_ptr<MaterialExecutor>& material )
    {

        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();
        material->Apply();

        const auto& vulkanLayout = std::static_pointer_cast<Graphic::API::Vulkan::VulkanPipeline>( pipeline );
        static_cast<VulkanMaterialBackend*>( material->GetMaterialBackend().get() )
             ->BindDescriptorSets( m_CurrentCommandBuffer, vulkanLayout->GetVkPipelineLayout(),
                                   VK_PIPELINE_BIND_POINT_GRAPHICS, frameIndex );
        vkCmdBindPipeline( m_CurrentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           vulkanLayout->GetVkPipeline() );

        VkDeviceSize offsets[] = { 0 };
        const auto   vbuffer =
             sp_cast<API::Vulkan::VulkanVertexBuffer>( mesh->GetVertexBuffer() )->GetVulkanBuffer();
        vkCmdBindVertexBuffers( m_CurrentCommandBuffer, 0, 1, &vbuffer, offsets );

        const auto ibuffer = sp_cast<API::Vulkan::VulkanIndexBuffer>( mesh->GetIndexBuffer() )->GetVulkanBuffer();
        vkCmdBindIndexBuffer( m_CurrentCommandBuffer, ibuffer, 0, VK_INDEX_TYPE_UINT32 );

        if ( ShouldUseDynamicLineWidth( pipeline ) )
        {
            vkCmdSetLineWidth( m_CurrentCommandBuffer, pipeline->GetSpecification().LineWidth );
        }

        const auto& pcBuffer = material->GetPushConstantBuffer();
        if ( pcBuffer.Size )
        {
            vkCmdPushConstants( m_CurrentCommandBuffer, vulkanLayout->GetVkPipelineLayout(),
                                VK_SHADER_STAGE_VERTEX_BIT, 0, pcBuffer.Size, pcBuffer.Data );
        }

        const auto& submeshes = mesh->GetSubmeshes();
        for ( const auto& submesh : submeshes )
        {
            vkCmdDrawIndexed( m_CurrentCommandBuffer, submesh.IndexCount, 1, submesh.IndexOffset,
                              submesh.VertexOffset, 0 );
        }
    }

    void VulkanRendererAPI::Shutdown()
    {
        vkDeviceWaitIdle( SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetMainDevice() )
                               ->GetVulkanLogicalDevice() );
    }

    VkCommandBuffer VulkanRendererAPI::GetCurrentCmdBuffer() const
    {
        return m_CurrentCommandBuffer;
    }

    void VulkanRendererAPI::ClearAttachments( const std::vector<VkClearValue>&    clearValues,
                                              const std::shared_ptr<Framebuffer>& framebuffer )
    {
        const uint32_t colorAttachmentCount = (uint32_t)framebuffer->GetColorAttachmentCount();
        const uint32_t totalAttachmentCount =
             colorAttachmentCount + ( ( framebuffer->GetDepthAttachmentCount() != 0 ) ? 1 : 0 );
        DESERT_VERIFY( clearValues.size() == totalAttachmentCount );

        std::vector<VkClearAttachment> attachments( totalAttachmentCount );
        std::vector<VkClearRect>       clearRects( totalAttachmentCount );
        for ( uint32_t i = 0; i < colorAttachmentCount; i++ )
        {
            attachments[i].aspectMask      = VK_IMAGE_ASPECT_COLOR_BIT;
            attachments[i].colorAttachment = i;
            attachments[i].clearValue      = clearValues[i];

            clearRects[i].rect.offset    = { (int32_t)0, (int32_t)0 };
            clearRects[i].rect.extent    = { framebuffer->GetFramebufferWidth(),
                                             framebuffer->GetFramebufferHeight() };
            clearRects[i].baseArrayLayer = 0;
            clearRects[i].layerCount     = 1;
        }

        if ( framebuffer->GetDepthAttachmentCount() != 0 )
        {
            attachments[colorAttachmentCount].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            attachments[colorAttachmentCount].clearValue = clearValues[colorAttachmentCount];
            clearRects[colorAttachmentCount].rect.offset = { (int32_t)0, (int32_t)0 };
            clearRects[colorAttachmentCount].rect.extent = { framebuffer->GetFramebufferWidth(),
                                                             framebuffer->GetFramebufferHeight() };
            clearRects[colorAttachmentCount].baseArrayLayer = 0;
            clearRects[colorAttachmentCount].layerCount     = 1;
        }

        vkCmdClearAttachments( m_CurrentCommandBuffer, totalAttachmentCount, attachments.data(),
                               totalAttachmentCount, clearRects.data() );
    }

} // namespace Desert::Graphic::API::Vulkan
