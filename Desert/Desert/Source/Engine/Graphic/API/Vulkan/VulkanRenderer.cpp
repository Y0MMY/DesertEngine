#include <Engine/Graphic/API/Vulkan/VulkanRenderer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderCommandBuffer.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanFramebuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanPipeline.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanVertexBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanIndexBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUniformBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Core/Camera.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic::API::Vulkan
{
    static Core::Camera s_Camera;

    Common::BoolResult VulkanRendererAPI::BeginFrame()
    {
        if ( m_CurrentCommandBuffer != nullptr )
        {
            return Common::MakeError<bool>( "BeginFrame(): Error! Have you call EndFrame() ?" );
        }

        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        VkCommandBufferBeginInfo cmdBufferBeginInfo{};
        cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        m_CurrentCommandBuffer = VulkanRenderCommandBuffer::GetInstance().GetCommandBuffer( frameIndex );
        vkBeginCommandBuffer( m_CurrentCommandBuffer, &cmdBufferBeginInfo );

        return Common::MakeSuccess( true );
    }

    Common::BoolResult VulkanRendererAPI::EndFrame()
    {
        if ( m_CurrentCommandBuffer == nullptr )
        {
            return Common::MakeError<bool>( "EndFrame(): Error! Have you call BeginFrame() ?" );
        }

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

    static struct ubo
    {
        glm::mat4 proj;
        glm::mat4 view;
    };

    void VulkanRendererAPI::Init()
    {
        m_UniformBuffer = UniformBuffer::Create( sizeof( ubo ), 0 );

       m_texture = TextureCube::Create( "Arches_E_PineTree_Radiance.tga" );
    }

    Common::BoolResult VulkanRendererAPI::BeginRenderPass( const std::shared_ptr<RenderPass>& renderPass )
    {
        uint32_t          frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();
        VkClearColorValue clearColor = { 1.0f, 0.0f, 0.0f, 0.0f }; // TODO: get from specification
        VkClearValue      clearValue;
        clearValue.color = clearColor;

        VkRenderPassBeginInfo renderPassBeginInfo = {
             .sType      = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
             .pNext      = NULL,
             .renderPass = std::static_pointer_cast<Graphic::API::Vulkan::VulkanFramebuffer>(
                                renderPass->GetSpecification().TargetFramebuffer )
                                ->GetRenderPass(),
             .renderArea      = { .offset = { .x = 0, .y = 0 }, .extent = { .width = 1920, .height = 780 } },
             .clearValueCount = 1,
             .pClearValues    = &clearValue };

        renderPassBeginInfo.framebuffer = std::static_pointer_cast<Graphic::API::Vulkan::VulkanFramebuffer>(
                                               renderPass->GetSpecification().TargetFramebuffer )
                                               ->GetFramebuffers()[frameIndex];

        vkCmdBeginRenderPass( m_CurrentCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE );

        return Common::MakeSuccess( true );
    }

    Common::BoolResult VulkanRendererAPI::EndRenderPass()
    {
        vkCmdEndRenderPass( m_CurrentCommandBuffer );
        return Common::MakeSuccess( true );
    }

    void VulkanRendererAPI::TEST_DrawTriangle( const std::shared_ptr<VertexBuffer>& vertexBuffer,
                                               const std::shared_ptr<IndexBuffer>&  indexBuffer,

                                               const std::shared_ptr<Pipeline>& pipeline )
    {
        UpdateDescriptorSets( pipeline );
        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        vkCmdBindPipeline(
             m_CurrentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanPipeline>( pipeline )->GetVkPipeline() );

        VkDeviceSize offsets[] = { 0 };
        const auto   buffer =
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanVertexBuffer>( vertexBuffer )->GetVulkanBuffer();
        vkCmdBindVertexBuffers( m_CurrentCommandBuffer, 0, 1, &buffer, offsets );

        const auto ibuffer =
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanIndexBuffer>( indexBuffer )->GetVulkanBuffer();

        vkCmdBindIndexBuffer( m_CurrentCommandBuffer, ibuffer, 0, VK_INDEX_TYPE_UINT32 );

        s_Camera.OnUpdate();

        auto vp = s_Camera.GetProjectionMatrix() * s_Camera.GetViewMatrix();

        ubo ubob{ s_Camera.GetProjectionMatrix(), s_Camera.GetViewMatrix() };
        m_UniformBuffer->RT_SetData( &ubob, sizeof( ubo ) );

        const auto shader =
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanShader>( pipeline->GetSpecification().Shader );

        VkDevice         device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();
        VkPipelineLayout layout =
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanPipeline>( pipeline )->GetVkPipelineLayout();
        vkCmdBindDescriptorSets( m_CurrentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1,
                                 &shader->GetVulkanDescriptorSetInfo().DescriptorSets.at( frameIndex ).at( 0 ), 0,
                                 nullptr );
        vkCmdDrawIndexed( m_CurrentCommandBuffer, indexBuffer->GetCount(), 1, 0, 0, 0 );
    }

    void VulkanRendererAPI::UpdateDescriptorSets( const std::shared_ptr<Pipeline>& pipeline )
    {
        auto vkImage = std::static_pointer_cast<Graphic::API::Vulkan::VulkanImage2D>( m_texture->GetImage2D() );
        auto info    = vkImage->GetVulkanImageInfo();

        VkDescriptorImageInfo imageInfo = {};
        imageInfo.sampler               = info.Sampler;
        imageInfo.imageView             = info.ImageView;
        imageInfo.imageLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        VkDevice   device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();
        const auto shader =
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanShader>( pipeline->GetSpecification().Shader );
        VkWriteDescriptorSet writeDescriptorSet =
             shader->GetWriteDescriptorSet( WriteDescriptorType::Uniform, 0, 0, frameIndex );
        const auto pBufferInfo =
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanUniformBuffer>( m_UniformBuffer )
                  ->GetDescriptorBufferInfo();
        writeDescriptorSet.pBufferInfo = &pBufferInfo;

        VkWriteDescriptorSet writeDescriptorSetImage = {};
        writeDescriptorSetImage.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSetImage.dstSet =
             shader->GetVulkanDescriptorSetInfo().DescriptorSets.at( frameIndex ).at( 0 );
        writeDescriptorSetImage.dstBinding      = 1;
        writeDescriptorSetImage.dstArrayElement = 0;
        writeDescriptorSetImage.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSetImage.descriptorCount = 1;
        writeDescriptorSetImage.pImageInfo      = &imageInfo;

        vkUpdateDescriptorSets( device, 1, &writeDescriptorSet, 0, nullptr );
        vkUpdateDescriptorSets( device, 1, &writeDescriptorSetImage, 0, nullptr );
    }

} // namespace Desert::Graphic::API::Vulkan