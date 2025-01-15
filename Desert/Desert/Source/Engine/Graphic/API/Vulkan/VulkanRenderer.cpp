#include <Engine/Graphic/API/Vulkan/VulkanRenderer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderCommandBuffer.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanFramebuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanPipeline.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanVertexBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUniformBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Core/Camera.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic::API::Vulkan
{
    static Core::Camera s_Camera;
    namespace
    {
        void InsertImageMemoryBarrier( VkCommandBuffer cmdbuffer, VkImage image, VkAccessFlags srcAccessMask,
                                       VkAccessFlags dstAccessMask, VkImageLayout oldImageLayout,
                                       VkImageLayout newImageLayout, VkPipelineStageFlags srcStageMask,
                                       VkPipelineStageFlags dstStageMask )
        {
            VkImageSubresourceRange subresourceRange = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                                         .baseMipLevel   = 0,
                                                         .levelCount     = 1,
                                                         .baseArrayLayer = 0,
                                                         .layerCount     = 1 };

            VkImageMemoryBarrier imageMemoryBarrier{};
            imageMemoryBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

            imageMemoryBarrier.srcAccessMask    = srcAccessMask;
            imageMemoryBarrier.dstAccessMask    = dstAccessMask;
            imageMemoryBarrier.oldLayout        = oldImageLayout;
            imageMemoryBarrier.newLayout        = newImageLayout;
            imageMemoryBarrier.image            = image;
            imageMemoryBarrier.subresourceRange = subresourceRange;

            vkCmdPipelineBarrier( cmdbuffer, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, 1,
                                  &imageMemoryBarrier );
        }
    } // namespace

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

    void VulkanRendererAPI::Init()
    {
        m_UniformBuffer = UniformBuffer::Create( sizeof( s_Camera ), 0 );
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
                                               const std::shared_ptr<Pipeline>&     pipeline )
    {

        vkCmdBindPipeline(
             m_CurrentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanPipeline>( pipeline )->GetVkPipeline() );

        VkDeviceSize offsets[] = { 0 };
        const auto   buffer =
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanVertexBuffer>( vertexBuffer )->GetVulkanBuffer();
        vkCmdBindVertexBuffers( m_CurrentCommandBuffer, 0, 1, &buffer, offsets );

        const auto pBufferInfo =
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanUniformBuffer>( m_UniformBuffer )
                  ->GetDescriptorBufferInfo();

        s_Camera.OnUpdate();

        auto vp = s_Camera.GetProjectionMatrix() * s_Camera.GetViewMatrix();
        m_UniformBuffer->RT_SetData( &vp[0], sizeof( glm::mat4 ) );

        const auto shader =
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanShader>( pipeline->GetSpecification().Shader );
        VkWriteDescriptorSet writeDescriptorSet = shader->GetDescriptorSet( "camera", 0 );

        writeDescriptorSet.pBufferInfo = &pBufferInfo;

        VkDevice         device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();
        VkPipelineLayout layout =
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanPipeline>( pipeline )->GetVkPipelineLayout();

        vkUpdateDescriptorSets( device, 1, &writeDescriptorSet, 0, nullptr );

        vkCmdBindDescriptorSets( m_CurrentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1,
                                 &std::static_pointer_cast<Graphic::API::Vulkan::VulkanPipeline>( pipeline )
                                       ->GetShaderDescriptorSet()
                                       .DescriptorSets[0],
                                 0, nullptr );
        vkCmdDraw( m_CurrentCommandBuffer, 3, 1, 0, 0 );
    }

} // namespace Desert::Graphic::API::Vulkan