#include <Engine/Graphic/API/Vulkan/VulkanRenderer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderCommandBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanFramebuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanPipeline.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanPipelineCompute.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanVertexBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanIndexBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanSwapChain.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanAllocator.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanMaterialBackend.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>
#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/WriteDescriptorSetBuilder.hpp>

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Core/EngineContext.hpp>
#include <Engine/Core/FrameManager.hpp>

namespace Desert::Graphic::API::Vulkan
{
    void VulkanRendererAPI::Init()
    {
    }
    void VulkanRendererAPI::Shutdown()
    {
    }

    Common::BoolResultStr VulkanRendererAPI::BeginFrame()
    {
        auto window = m_Window.lock();
        if ( !window )
            return Common::MakeError( "Window is null" );

        auto swapChain         = SP_CAST( VulkanSwapChain, window->GetWindowSwapChain() );
        m_CurrentCommandBuffer = swapChain->GetVulkanQueue()->GetDrawCommandBuffer();

        // Reset descriptor update tracking for this frame
        // This requires access to materials, which might be hard here.
        // A better approach is to have MaterialBackend reset itself.

        VkCommandBufferBeginInfo beginInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                               .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
        VK_CHECK_RESULT_BOOL( vkBeginCommandBuffer( m_CurrentCommandBuffer, &beginInfo ) );

        return BOOLSUCCESS;
    }

    Common::BoolResultStr VulkanRendererAPI::EndFrame()
    {
        if ( m_CurrentCommandBuffer )
        {
            VK_CHECK_RESULT_BOOL( vkEndCommandBuffer( m_CurrentCommandBuffer ) );
            m_CurrentCommandBuffer = nullptr;
        }
        return BOOLSUCCESS;
    }

    Common::BoolResultStr VulkanRendererAPI::PrepareNextFrame()
    {
        auto window = m_Window.lock();
        if ( !window )
            return Common::MakeError( "Window is null" );
        SP_CAST( VulkanSwapChain, window->GetWindowSwapChain() )->PrepareFrame();
        return BOOLSUCCESS;
    }

    Common::BoolResultStr VulkanRendererAPI::PresentFinalImage()
    {
        auto window = m_Window.lock();
        if ( !window )
            return Common::MakeError( "Window is null" );
        auto swapChain = SP_CAST( VulkanSwapChain, window->GetWindowSwapChain() );

        EndFrame();
        swapChain->GetVulkanQueue()->Submit();
        swapChain->Present();

        return BOOLSUCCESS;
    }

    Common::BoolResultStr VulkanRendererAPI::BeginRenderPass( const RenderPass* renderPass, bool clearFrame )
    {
        if ( !m_CurrentCommandBuffer )
            return Common::MakeError( "No active command buffer" );

        const auto framebuffer       = renderPass->GetSpecification().TargetFramebuffer;
        const auto vulkanFramebuffer = sp_cast<VulkanFramebuffer>( framebuffer );

        VkRenderPassBeginInfo renderPassInfo = {
             .sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
             .renderPass  = clearFrame ? vulkanFramebuffer->GetVKRenderPass()
                                       : vulkanFramebuffer->GetVKRenderPassLoad(),
             .framebuffer = vulkanFramebuffer->GetVKFramebuffer(),
             .renderArea  = {
                   .offset = { 0, 0 },
                   .extent = { framebuffer->GetFramebufferWidth(), framebuffer->GetFramebufferHeight() } } };

        const auto& clearSpec = renderPass->GetSpecification().ClearColor;
        std::vector<VkClearValue> clearValues;
        for ( const auto& attachment : framebuffer->GetSpecification().Attachments.Attachments )
        {
            VkClearValue clearValue{};
            if ( Graphic::Utils::IsDepthFormat( attachment.Format ) )
            {
                clearValue.depthStencil = { clearSpec.DepthStencil.x, static_cast<uint32_t>( clearSpec.DepthStencil.y ) };
            }
            else
            {
                clearValue.color = { { clearSpec.Color.r, clearSpec.Color.g, clearSpec.Color.b, clearSpec.Color.a } };
            }
            clearValues.push_back( clearValue );
        }

        renderPassInfo.clearValueCount = static_cast<uint32_t>( clearValues.size() );
        renderPassInfo.pClearValues    = clearValues.data();

        vkCmdBeginRenderPass( m_CurrentCommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE );
        SetViewportAndScissor( framebuffer->GetFramebufferWidth(), framebuffer->GetFramebufferHeight() );

        return BOOLSUCCESS;
    }

    Common::BoolResultStr VulkanRendererAPI::BeginSwapChainRenderPass()
    {
        if ( !m_CurrentCommandBuffer )
            return Common::MakeError( "No active command buffer" );

        auto window            = m_Window.lock();
        auto vulkanSwap        = SP_CAST( VulkanSwapChain, window->GetWindowSwapChain() );
        auto framebuffer       = vulkanSwap->GetCompositeFramebuffer();
        m_CompositeFramebuffer = framebuffer;

        uint32_t imageIndex = vulkanSwap->GetCurrentBufferIndex();

        VkRenderPassBeginInfo renderPassInfo = {
             .sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
             .renderPass  = vulkanSwap->GetRenderPass(),
             .framebuffer = vulkanSwap->GetVKFramebuffers()[imageIndex],
             .renderArea  = {
                   .offset = { 0, 0 },
                   .extent = { framebuffer->GetFramebufferWidth(), framebuffer->GetFramebufferHeight() } } };

        VkClearValue clearValue        = { .color = { { 0.1f, 0.1f, 0.1f, 1.0f } } };
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues    = &clearValue;

        vkCmdBeginRenderPass( m_CurrentCommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE );
        SetViewportAndScissor( framebuffer->GetFramebufferWidth(), framebuffer->GetFramebufferHeight() );

        return BOOLSUCCESS;
    }

    Common::BoolResultStr VulkanRendererAPI::EndRenderPass()
    {
        if ( m_CurrentCommandBuffer )
        {
            vkCmdEndRenderPass( m_CurrentCommandBuffer );

            // The implicit final subpass dependency uses dstStageMask=BOTTOM_OF_PIPE and
            // dstAccessMask=0, which makes color writes *available* (flushed from the
            // attachment cache) but NOT *visible* to subsequent fragment-shader texture reads.
            // Without this barrier the shader texture cache (L1) is never invalidated, so the
            // next pass — or the matching pass in the next frame — samples stale data, producing
            // every-other-frame flickering with 2+ frames in flight.
            VkMemoryBarrier memBarrier = {
                .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            };
            vkCmdPipelineBarrier( m_CurrentCommandBuffer,
                                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                  0,
                                  1, &memBarrier,
                                  0, nullptr,
                                  0, nullptr );
        }
        return BOOLSUCCESS;
    }

    void VulkanRendererAPI::RenderMesh( const GraphicsPipeline* pipeline, const Mesh* mesh,
                                        const glm::mat4 transform, const MaterialExecutor* materialExecutor )
    {
        if ( !m_CurrentCommandBuffer )
            return;
        const auto vulkanPipeline = static_cast<const VulkanPipeline*>( pipeline );
        vkCmdBindPipeline( m_CurrentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           vulkanPipeline->GetVkPipeline() );

        // Bind Descriptor Sets
        if ( materialExecutor )
        {
            materialExecutor->Apply();
            auto vkBackend = static_cast<VulkanMaterialBackend*>( materialExecutor->GetMaterialBackend().get() );

            if ( !vkBackend->HasDescriptorSets() )
            {
                LOG_WARN( "VulkanRendererAPI: MaterialExecutor has no valid descriptor sets!" );
                return;
            }

            uint32_t frameIndex = Engine::FrameManager::GetInstance().GetCurrentFrameIndex();
            vkBackend->BindDescriptorSets( m_CurrentCommandBuffer, vulkanPipeline->GetVkPipelineLayout(),
                                           VK_PIPELINE_BIND_POINT_GRAPHICS, frameIndex );
        }

        VkDeviceSize offsets[] = { 0 };
        auto vbuffer = sp_cast<API::Vulkan::VulkanVertexBuffer>( mesh->GetVertexBuffer() )->GetVulkanBuffer();
        vkCmdBindVertexBuffers( m_CurrentCommandBuffer, 0, 1, &vbuffer, offsets );

        if ( auto indexBuffer = mesh->GetIndexBuffer() )
        {
            auto ibuffer = sp_cast<API::Vulkan::VulkanIndexBuffer>( indexBuffer )->GetVulkanBuffer();
            vkCmdBindIndexBuffer( m_CurrentCommandBuffer, ibuffer, 0, VK_INDEX_TYPE_UINT32 );
        }

        const auto& submeshes = mesh->GetSubmeshes();
        for ( const auto& submesh : submeshes )
        {
            MaterialExecutor* materialExec   = (MaterialExecutor*)materialExecutor;
            auto              finalTransform = transform * submesh.Transform;
            materialExec->PushConstant( &finalTransform, sizeof( glm::mat4 ) );

            const auto&   pcBuffer     = materialExecutor->GetPushConstantBuffer();
            VulkanShader* vulkanShader = (VulkanShader*)pipeline->GetSpecification().Shader.get();
            if ( pcBuffer.Size && vulkanShader->GetShaderPushConstant().has_value() )
            {
                auto pcInfo = vulkanShader->GetShaderPushConstant().value();
                vkCmdPushConstants( m_CurrentCommandBuffer, vulkanPipeline->GetVkPipelineLayout(),
                                    (VkShaderStageFlags)pcInfo.ShaderStage, 0, (uint32_t)pcBuffer.Size,
                                    pcBuffer.Data );
            }

            if ( mesh->GetIndexBuffer() )
            {
                if ( submesh.IndexOffset + submesh.IndexCount > mesh->GetIndexBuffer()->GetCount() )
                {
                    LOG_ERROR(
                         "VulkanRendererAPI: Invalid index buffer access! Offset: {}, Count: {}, BufferSize: {}",
                         submesh.IndexOffset, submesh.IndexCount, mesh->GetIndexBuffer()->GetCount() );
                    continue;
                }

                vkCmdDrawIndexed( m_CurrentCommandBuffer, submesh.IndexCount, 1, submesh.IndexOffset,
                                  (int32_t)submesh.VertexOffset, 0 );
            }
            else
            {
                vkCmdDraw( m_CurrentCommandBuffer, submesh.VertexCount, 1, submesh.VertexOffset, 0 );
            }
        }
    }

    void VulkanRendererAPI::SubmitFullscreenQuad( const GraphicsPipeline* pipeline,
                                                  const MaterialExecutor* materialExecutor )
    {
        if ( !m_CurrentCommandBuffer )
            return;
        const auto vulkanPipeline = static_cast<const VulkanPipeline*>( pipeline );
        vkCmdBindPipeline( m_CurrentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           vulkanPipeline->GetVkPipeline() );

        // Bind Descriptor Sets
        if ( materialExecutor )
        {
            materialExecutor->Apply();
            auto vkBackend = static_cast<VulkanMaterialBackend*>( materialExecutor->GetMaterialBackend().get() );

            if ( !vkBackend->HasDescriptorSets() )
            {
                LOG_WARN( "VulkanRendererAPI: MaterialExecutor has no valid descriptor sets!" );
                return;
            }

            uint32_t frameIndex = Engine::FrameManager::GetInstance().GetCurrentFrameIndex();
            vkBackend->BindDescriptorSets( m_CurrentCommandBuffer, vulkanPipeline->GetVkPipelineLayout(),
                                           VK_PIPELINE_BIND_POINT_GRAPHICS, frameIndex );
        }

        const auto&   pcBuffer     = materialExecutor->GetPushConstantBuffer();
        VulkanShader* vulkanShader = (VulkanShader*)pipeline->GetSpecification().Shader.get();
        if ( pcBuffer.Size && vulkanShader->GetShaderPushConstant().has_value() )
        {
            auto pcInfo = vulkanShader->GetShaderPushConstant().value();
            vkCmdPushConstants( m_CurrentCommandBuffer, vulkanPipeline->GetVkPipelineLayout(),
                                (VkShaderStageFlags)pcInfo.ShaderStage, 0, (uint32_t)pcBuffer.Size,
                                pcBuffer.Data );
        }

        vkCmdDraw( m_CurrentCommandBuffer, 6, 1, 0, 0 );
    }

    void VulkanRendererAPI::DispatchCompute( const ComputePipeline* pipeline, uint32_t groupCountX,
                                             uint32_t groupCountY, uint32_t groupCountZ,
                                             const MaterialExecutor* materialExecutor )
    {
        if ( !m_CurrentCommandBuffer )
            return;
        const auto vulkanPipeline = static_cast<const VulkanPipelineCompute*>( pipeline );
        vkCmdBindPipeline( m_CurrentCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                           vulkanPipeline->GetVkPipeline() );

        // Bind Descriptor Sets
        if ( materialExecutor )
        {
            materialExecutor->Apply();
            auto vkBackend = static_cast<VulkanMaterialBackend*>( materialExecutor->GetMaterialBackend().get() );
            uint32_t frameIndex = Engine::FrameManager::GetInstance().GetCurrentFrameIndex();
            vkBackend->BindDescriptorSets( m_CurrentCommandBuffer, vulkanPipeline->GetVkPipelineLayout(),
                                           VK_PIPELINE_BIND_POINT_COMPUTE, frameIndex );
        }

        vkCmdDispatch( m_CurrentCommandBuffer, groupCountX, groupCountY, groupCountZ );
    }

    void VulkanRendererAPI::ImmediateComputeDispatch( const ComputePipeline* pipeline,
                                                      Image2D*   inputImage,
                                                      ImageCube* outputImage,
                                                      uint32_t groupCountX, uint32_t groupCountY,
                                                      uint32_t groupCountZ )
    {
        auto vkPipeline = static_cast<const VulkanPipelineCompute*>( pipeline );
        auto vkInput    = dynamic_cast<IVulkanImage*>( inputImage );
        auto vkOutput   = dynamic_cast<IVulkanImage*>( outputImage );

        if ( !vkPipeline || !vkInput || !vkOutput )
            return;

        auto* backend = vkPipeline->GetVulkanMaterialBackend();
        if ( !backend )
            return;

        auto cmdResult = CommandBufferAllocator::GetInstance().RT_GetCommandBufferCompute( true );
        if ( !cmdResult.IsSuccess() )
            return;
        VkCommandBuffer cmd = cmdResult.GetValue();

        // Transition output cubemap to GENERAL so compute can write to it
        vkOutput->TransitionLayout( cmd, VK_IMAGE_LAYOUT_GENERAL );

        // Update descriptor set 0 with the real input sampler and output storage image
        const auto& inRes  = vkInput->GetResource();
        const auto& outRes = vkOutput->GetResource();

        VkDescriptorImageInfo samplerInfo = { inRes.Sampler, inRes.ImageView,
                                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo storageInfo = { VK_NULL_HANDLE, outRes.ImageView, VK_IMAGE_LAYOUT_GENERAL };

        auto wds0 = DescriptorSetBuilder::GetSampler2DWDS( backend, 0, 0, 0, 1, &samplerInfo );
        auto wds1 = DescriptorSetBuilder::GetStorageWDS( backend, 0, 0, 1, 1, &storageInfo );
        backend->UpdateDescriptorSets( { wds0, wds1 } );

        // Bind and dispatch
        vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipeline->GetVkPipeline() );
        backend->BindDescriptorSets( cmd, vkPipeline->GetVkPipelineLayout(),
                                     VK_PIPELINE_BIND_POINT_COMPUTE, 0 );
        vkCmdDispatch( cmd, groupCountX, groupCountY, groupCountZ );

        // Transition output to SHADER_READ_ONLY so it can be sampled by the skybox shader
        vkOutput->TransitionLayout( cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );

        CommandBufferAllocator::GetInstance().RT_FlushCommandBufferCompute( cmd );
    }

    void VulkanRendererAPI::ResizeWindowEvent( uint32_t width, uint32_t height )
    {
    }

    void VulkanRendererAPI::WaitDeviceIdle()
    {
        vkDeviceWaitIdle( SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetDevice() )
                               ->GetVulkanLogicalDevice() );
    }
    std::shared_ptr<Framebuffer> VulkanRendererAPI::GetCompositeFramebuffer() const
    {
        return m_CompositeFramebuffer.lock();
    }
    VkCommandBuffer VulkanRendererAPI::GetCurrentCmdBuffer() const
    {
        return m_CurrentCommandBuffer;
    }

    void VulkanRendererAPI::SetViewportAndScissor( const uint32_t width, const uint32_t height )
    {
        if ( !m_CurrentCommandBuffer )
            return;
        if ( width == 0 || height == 0 )
            return;

        // Modern Vulkan: use negative height to flip Y coordinate system to match OpenGL (Y-up)
        VkViewport viewport = { .x        = 0.0f,
                                .y        = (float)height,
                                .width    = (float)width,
                                .height   = -(float)height,
                                .minDepth = 0.0f,
                                .maxDepth = 1.0f };
        vkCmdSetViewport( m_CurrentCommandBuffer, 0, 1, &viewport );

        VkRect2D scissor = { .offset = { 0, 0 }, .extent = { width, height } };
        vkCmdSetScissor( m_CurrentCommandBuffer, 0, 1, &scissor );
    }

    void VulkanRendererAPI::ClearAttachments( const std::vector<VkClearValue>&    clearValues,
                                              const std::shared_ptr<Framebuffer>& framebuffer )
    {
        if ( !m_CurrentCommandBuffer )
            return;
        uint32_t                       attachmentCount = (uint32_t)clearValues.size();
        std::vector<VkClearAttachment> attachments( attachmentCount );
        std::vector<VkClearRect>       clearRects( attachmentCount );
        for ( uint32_t i = 0; i < attachmentCount; i++ )
        {
            attachments[i] = {
                 .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .colorAttachment = i, .clearValue = clearValues[i] };
            if ( Graphic::Utils::IsDepthFormat( framebuffer->GetSpecification().Attachments.Attachments[i].Format ) )
                attachments[i].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            clearRects[i] = {
                 .rect           = { .offset = { 0, 0 },
                                     .extent = { framebuffer->GetFramebufferWidth(), framebuffer->GetFramebufferHeight() } },
                 .baseArrayLayer = 0,
                 .layerCount     = 1 };
        }
        vkCmdClearAttachments( m_CurrentCommandBuffer, attachmentCount, attachments.data(), attachmentCount,
                               clearRects.data() );
    }

} // namespace Desert::Graphic::API::Vulkan
