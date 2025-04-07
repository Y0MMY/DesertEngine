#include <Engine/Graphic/API/Vulkan/VulkanRenderer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderCommandBuffer.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanFramebuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanPipeline.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanPipelineCompute.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanVertexBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanIndexBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUniformBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>
#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Core/EngineContext.h>

#include <Engine/Graphic/API/Vulkan/imgui/ImGuiRenderer.hpp>

#include "stb_image/stb_image_write.h"

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Core/Camera.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic::API::Vulkan
{
    struct VulkanRendererData
    {
        std::shared_ptr<VulkanVertexBuffer> QuadVertexBuffer;
        std::shared_ptr<VulkanIndexBuffer>  QuadIndexBuffer;
    };

    static VulkanRendererData* s_Data = nullptr;

    Common::BoolResult VulkanRendererAPI::BeginFrame()
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

    Common::BoolResult VulkanRendererAPI::EndFrame()
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

    Common::BoolResult VulkanRendererAPI::PresentFinalImage()
    {
        std::static_pointer_cast<Graphic::API::Vulkan::VulkanContext>(
             Renderer::GetInstance().GetRendererContext() )
             ->PresentFinalImage();

        return Common::MakeSuccess( true );
    }

    void VulkanRendererAPI::Init()
    {
        s_Data = new VulkanRendererData;

        struct QuadVertex
        {
            glm::vec3 Position;
        };

        std::array<QuadVertex, 4> data;
        data[0].Position = { -1, 1, 0 };

        data[1].Position = { -1, -1, 0 };

        data[2].Position = { 1, 1, 0 };

        data[3].Position = { 1, -1, 0 };

        s_Data->QuadVertexBuffer = std::make_shared<VulkanVertexBuffer>( data.data(), 4 * sizeof( QuadVertex ) );
        s_Data->QuadVertexBuffer->Invalidate();

        uint32_t* indices = new uint32_t[6]{
             0, 1, 2, 1, 2, 3,
        };

        s_Data->QuadIndexBuffer = std::make_shared<VulkanIndexBuffer>( indices, 6 * sizeof( unsigned int ) );
        s_Data->QuadIndexBuffer->Invalidate();

        // delete[] indices;
    }

    std::array<VkClearValue, 2> CreateClearValues( const std::shared_ptr<RenderPass>& renderPass )
    {
        const auto                  clearColor = renderPass->GetSpecification().ClearColor;
        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = { clearColor.Color.x, clearColor.Color.y, clearColor.Color.z, clearColor.Color.w };
        clearValues[1].depthStencil = { clearColor.DepthStencil.x,
                                        static_cast<uint32_t>( clearColor.DepthStencil.y ) };
        return clearValues;
    }

    std::shared_ptr<VulkanFramebuffer> GetFramebuffer( const std::shared_ptr<RenderPass>& renderPass )
    {
        return sp_cast<Graphic::API::Vulkan::VulkanFramebuffer>(
             renderPass->GetSpecification().TargetFramebuffer );
    }

    std::shared_ptr<VulkanSwapChain> GetSwapChain()
    {
        return sp_cast<Graphic::API::Vulkan::VulkanContext>( Renderer::GetInstance().GetRendererContext() )
             ->GetVulkanSwapChain();
    }

    VkRenderPassBeginInfo CreateRenderPassBeginInfo( const std::shared_ptr<VulkanSwapChain>&   swapChain,
                                                     const std::shared_ptr<VulkanFramebuffer>& framebuffer,
                                                     const std::array<VkClearValue, 2>&        clearValues )
    {
        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType                 = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass            = framebuffer->GetVKRenderPass();
        renderPassBeginInfo.renderArea.offset     = { 0, 0 };
        renderPassBeginInfo.renderArea.extent     = { framebuffer->GetFramebufferWidth(),
                                                      framebuffer->GetFramebufferHeight() };
        renderPassBeginInfo.clearValueCount       = static_cast<uint32_t>( clearValues.size() );
        renderPassBeginInfo.pClearValues          = clearValues.data();
        renderPassBeginInfo.framebuffer           = framebuffer->GetVKFramebuffer();
        return renderPassBeginInfo;
    }

    VkRenderPassBeginInfo CreateRenderPassBeginInfo( const std::shared_ptr<VulkanSwapChain>& swapChain,
                                                     const VkFramebuffer                     framebuffer,
                                                     const std::array<VkClearValue, 2>&      clearValues )
    {
        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType                 = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass            = swapChain->GetRenderPass();
        renderPassBeginInfo.renderArea.offset     = { 0, 0 };
        renderPassBeginInfo.renderArea.extent     = { 1920, 780 };
        renderPassBeginInfo.clearValueCount       = static_cast<uint32_t>( clearValues.size() );
        renderPassBeginInfo.pClearValues          = clearValues.data();
        renderPassBeginInfo.framebuffer           = framebuffer;
        return renderPassBeginInfo;
    }

    Common::BoolResult VulkanRendererAPI::BeginRenderPass( const std::shared_ptr<RenderPass>& renderPass )
    {
        auto        clearValues = CreateClearValues( renderPass );
        auto        framebuffer = GetFramebuffer( renderPass );
        const auto& swapChain   = GetSwapChain();

        m_CompositeFramebuffer = framebuffer;

        SetViewportAndScissor();

        VkRenderPassBeginInfo renderPassBeginInfo =
             CreateRenderPassBeginInfo( swapChain, framebuffer, clearValues );
        vkCmdBeginRenderPass( m_CurrentCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE );

        return Common::MakeSuccess( true );
    }

    Common::BoolResult VulkanRendererAPI::BeginSwapChainRenderPass()
    {
        uint32_t                    frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();
        std::array<VkClearValue, 2> clearValues{};
        const auto&                 swapChain   = GetSwapChain();
        const auto                  framebuffer = swapChain->GetVKFramebuffers();

        SetViewportAndScissor();

        VkRenderPassBeginInfo renderPassBeginInfo =
             CreateRenderPassBeginInfo( swapChain, framebuffer[frameIndex], clearValues );
        vkCmdBeginRenderPass( m_CurrentCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE );

        return Common::MakeSuccess( true );
    }

    Common::BoolResult VulkanRendererAPI::EndRenderPass()
    {
        vkCmdEndRenderPass( m_CurrentCommandBuffer );
        return Common::MakeSuccess( true );
    }

    void VulkanRendererAPI::SubmitFullscreenQuad( const std::shared_ptr<Pipeline>& pipeline )
    {
        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        const auto shader =
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanShader>( pipeline->GetSpecification().Shader );
        const auto& desSet = shader->GetVulkanDescriptorSetInfo().DescriptorSets.at( frameIndex ).at( 0 );

        VkPipelineLayout layout =
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanPipeline>( pipeline )->GetVkPipelineLayout();
        vkCmdBindDescriptorSets( m_CurrentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &desSet, 0,
                                 nullptr );

        vkCmdBindPipeline(
             m_CurrentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanPipeline>( pipeline )->GetVkPipeline() );

        VkDeviceSize offsets[] = { 0 };
        const auto   vbuffer   = s_Data->QuadVertexBuffer->GetVulkanBuffer();
        vkCmdBindVertexBuffers( m_CurrentCommandBuffer, 0, 1, &vbuffer, offsets );

        const auto ibuffer = s_Data->QuadIndexBuffer->GetVulkanBuffer();
        vkCmdBindIndexBuffer( m_CurrentCommandBuffer, ibuffer, 0, VK_INDEX_TYPE_UINT32 );

        VkDevice device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

        vkCmdDrawIndexed( m_CurrentCommandBuffer, s_Data->QuadIndexBuffer->GetCount(), 1, 0, 0, 0 );
    }

    void VulkanRendererAPI::ResizeWindowEvent( uint32_t width, uint32_t height,
                                               const std::vector<std::shared_ptr<Framebuffer>>& framebuffers )
    {
        vkDeviceWaitIdle( VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice() );

        const auto& swapChain = std::static_pointer_cast<Graphic::API::Vulkan::VulkanContext>(
                                     Renderer::GetInstance().GetRendererContext() )
                                     ->GetVulkanSwapChain();

        swapChain->OnResize( width, height );
        for ( auto& fb : framebuffers )
        {
            fb->Resize( width, height );
        }
    }

    void VulkanRendererAPI::SetViewportAndScissor()
    {
        uint32_t windowWidth  = EngineContext::GetInstance().GetCurrentWindowWidth();
        uint32_t windowHeight = EngineContext::GetInstance().GetCurrentWindowHeight();

        VkViewport viewport = {};
        viewport.x          = 0.0f;
        viewport.y          = 0.0f;
        viewport.width      = static_cast<float>( windowWidth );
        viewport.height     = static_cast<float>( windowHeight );
        viewport.minDepth   = 0.0f;
        viewport.maxDepth   = 1.0f;

        VkRect2D scissor = {};
        scissor.offset   = { 0, 0 };
        scissor.extent   = { windowWidth, windowHeight };

        vkCmdSetViewport( m_CurrentCommandBuffer, 0, 1, &viewport );
        vkCmdSetScissor( m_CurrentCommandBuffer, 0, 1, &scissor );
    }

    std::shared_ptr<Framebuffer> VulkanRendererAPI::GetCompositeFramebuffer() const
    {
        return m_CompositeFramebuffer;
    }

    void VulkanRendererAPI::RenderImGui()
    {
        ImGui::VulkanImGuiRenderer::GetInstance().RenderImGui( m_CurrentCommandBuffer );
    }

    void VulkanRendererAPI::RenderMesh( const std::shared_ptr<Pipeline>& pipeline,
                                        const std::shared_ptr<Mesh>& mesh, const glm::mat4& mvp /*TEMP*/ )
    {
        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        /* const auto shader =
              std::static_pointer_cast<Graphic::API::Vulkan::VulkanShader>( pipeline->GetSpecification().Shader );
         const auto& desSet = shader->GetVulkanDescriptorSetInfo().DescriptorSets.at( frameIndex ).at( 0 );
         */
        VkPipelineLayout layout =
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanPipeline>( pipeline )->GetVkPipelineLayout();
        /*  vkCmdBindDescriptorSets( m_CurrentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1,
          &desSet, 0, nullptr );*/

        vkCmdBindPipeline(
             m_CurrentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanPipeline>( pipeline )->GetVkPipeline() );

        VkDeviceSize offsets[] = { 0 };
        const auto   vbuffer =
             sp_cast<API::Vulkan::VulkanVertexBuffer>( mesh->GetVertexBuffer() )->GetVulkanBuffer();
        vkCmdBindVertexBuffers( m_CurrentCommandBuffer, 0, 1, &vbuffer, offsets );

        const auto ibuffer = sp_cast<API::Vulkan::VulkanIndexBuffer>( mesh->GetIndexBuffer() )->GetVulkanBuffer();
        vkCmdBindIndexBuffer( m_CurrentCommandBuffer, ibuffer, 0, VK_INDEX_TYPE_UINT32 );

        // VkDevice device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

        vkCmdPushConstants( m_CurrentCommandBuffer, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, &mvp );

        vkCmdDrawIndexed( m_CurrentCommandBuffer, mesh->GetIndexBuffer()->GetCount(), 1, 0, 0, 0 );
    }

    std::shared_ptr<Desert::Graphic::Image2D>
    VulkanRendererAPI::EquirectangularToCubeMap( const Common::Filepath& filepath )
    {
        static std::shared_ptr<Shader>    shader        = Shader::Create( "comute_test.glsl" );
        static std::shared_ptr<Texture2D> imagePanorama = Texture2D::Create( filepath );
        const auto& imageVulkanCube = sp_cast<API::Vulkan::VulkanImage2D>( imagePanorama->GetImage2D() );

        Core::Formats::ImageSpecification outputImageInfo;
        outputImageInfo.Width      = imagePanorama->GetImage2D()->GetWidth();
        outputImageInfo.Height     = ( imagePanorama->GetImage2D()->GetHeight() * 1.5f );
        outputImageInfo.Format     = Core::Formats::ImageFormat::RGBA32F;
        outputImageInfo.Properties = Core::Formats::Storage;

        const std::shared_ptr<Image2D> outputImage       = Image2D::Create( outputImageInfo );
        const auto&                    outputImageVulkan = sp_cast<API::Vulkan::VulkanImage2D>( outputImage );
        outputImageVulkan->RT_Invalidate();

        const auto& shaderVulkan = sp_cast<API::Vulkan::VulkanShader>( shader );

        std::array<VkDescriptorImageInfo, 2> imageInfo = {};
        imageInfo[0].imageView                         = outputImageVulkan->GetVulkanImageInfo().ImageView;
        imageInfo[0].imageLayout                       = VK_IMAGE_LAYOUT_GENERAL;

        imageInfo[1].imageView   = imageVulkanCube->GetVulkanImageInfo().ImageView;
        imageInfo[1].sampler     = imageVulkanCube->GetVulkanImageInfo().Sampler;
        imageInfo[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        auto vulkanPipelineCompute = Graphic::PipelineCompute::Create( shader );
        vulkanPipelineCompute->Invalidate();

        std::array<VkWriteDescriptorSet, 2> descriptorWrite = {};

        auto descriptorSet = shaderVulkan->GetWriteDescriptorSet(
             API::Vulkan::WriteDescriptorType::StorageSampler2D, 1, 0, frameIndex );
        descriptorWrite[0] = descriptorSet;
        descriptorWrite[0].dstSet =
             shaderVulkan->GetVulkanDescriptorSetInfo().DescriptorSets.at( frameIndex ).at( 0 );
        descriptorWrite[0].pImageInfo = &imageInfo[0];

        descriptorSet =
             shaderVulkan->GetWriteDescriptorSet( API::Vulkan::WriteDescriptorType::Sampler2D, 0, 0, frameIndex );

        descriptorWrite[1] = descriptorSet;
        descriptorWrite[1].dstSet =
             shaderVulkan->GetVulkanDescriptorSetInfo().DescriptorSets.at( frameIndex ).at( 0 );
        descriptorWrite[1].pImageInfo = &imageInfo[1];

        VkDevice device = API::Vulkan::VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();
        vkUpdateDescriptorSets( device, descriptorWrite.size(), descriptorWrite.data(), 0, nullptr );

        vulkanPipelineCompute->Begin();
        sp_cast<API::Vulkan::VulkanPipelineCompute>( vulkanPipelineCompute )->BindDS( descriptorWrite[0].dstSet );
        vulkanPipelineCompute->Dispatch( outputImageInfo.Width / 32, outputImageInfo.Height / 32, 1 );
        vulkanPipelineCompute->End();

       /* const auto& result = outputImage->GetImagePixels();

        const char* outputPath = "output123.tga";
        const auto  res        = std::get<std::vector<unsigned char>>( result );
        int success = stbi_write_tga( outputPath, outputImageInfo.Width, outputImageInfo.Height, 4, res.data() );*/

        return std::shared_ptr<Desert::Graphic::Image2D>( outputImage );
    }

} // namespace Desert::Graphic::API::Vulkan