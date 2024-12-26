#include <Engine/Graphic/API/Vulkan/VulkanRenderer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderCommandBuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanFramebuffer.hpp>  //temp
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>       //temp
#include <Engine/Graphic/API/Vulkan/VulkanPipeline.hpp>     //temp
#include <Engine/Graphic/API/Vulkan/VulkanVertexBuffer.hpp> //temp
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/VertexBuffer.hpp>

namespace Desert::Graphic::API::Vulkan
{
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

    void VulkanRendererAPI::BeginFrame()
    {
        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        VkCommandBufferBeginInfo cmdBufferBeginInfo{};
        cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        m_CurrentCommandBuffer = VulkanRenderCommandBuffer::GetInstance().GetCommandBuffer( frameIndex );
        vkBeginCommandBuffer( m_CurrentCommandBuffer, &cmdBufferBeginInfo );

        VkClearColorValue ClearColor = { 1.0f, 0.0f, 0.0f, 0.0f }; // TODO: TEMP
        VkClearValue      ClearValue;
        ClearValue.color = ClearColor;

        VkRenderPassBeginInfo renderPassBeginInfo = {
             .sType      = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
             .pNext      = NULL,
             .renderPass = std::static_pointer_cast<Graphic::API::Vulkan::VulkanFramebuffer>( m_framebuffer )
                                ->GetRenderPass(),
             .renderArea      = { .offset = { .x = 0, .y = 0 }, .extent = { .width = 1920, .height = 780 } },
             .clearValueCount = 1,
             .pClearValues    = &ClearValue };

        renderPassBeginInfo.framebuffer =
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanFramebuffer>( m_framebuffer )
                  ->GetFramebuffers()[frameIndex];

        vkCmdBeginRenderPass( m_CurrentCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE );

        vkCmdBindPipeline(
             m_CurrentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
             std::static_pointer_cast<Graphic::API::Vulkan::VulkanPipeline>( m_Pipeline )->GetVkPipeline() );

        VkDeviceSize offsets[] = { 0 }; 
        const auto   buffer = std::static_pointer_cast<Graphic::API::Vulkan::VulkanVertexBuffer>( m_VertexBuffer )
                                 ->GetVulkanBuffer();
        vkCmdBindVertexBuffers( m_CurrentCommandBuffer, 0, 1, &buffer, offsets );

        vkCmdDraw( m_CurrentCommandBuffer, 3, 1, 0, 0 );
        vkCmdEndRenderPass( m_CurrentCommandBuffer );
    }

    void VulkanRendererAPI::EndFrame()
    {
        VkResult res = vkEndCommandBuffer( m_CurrentCommandBuffer );

        m_CurrentCommandBuffer = nullptr;
    }

    void VulkanRendererAPI::PresentFinalImage()
    {
        std::static_pointer_cast<Graphic::API::Vulkan::VulkanContext>(
             Renderer::GetInstance().GetRendererContext() )
             ->PresentFinalImage();
    }

    void VulkanRendererAPI::Init()
    {
        m_framebuffer = Framebuffer::Create( {} );
        m_framebuffer->Resize( 1, 1 );
        m_Shader = Shader::Create( "test.glsl" );
        PipelineSpecification spec;

        float* vertices = new float[9]{
             0.0f,  -0.5f, 0.0f, // Нижний угол (по оси Y)
             0.5f,  0.5f,  0.0f, // Верхний правый угол
             -0.5f, 0.5f,  0.0f  // Верхний левый угол
        };

        m_VertexBuffer = VertexBuffer::Create( vertices, 9 * 4 );
        std::static_pointer_cast<Graphic::API::Vulkan::VulkanVertexBuffer>(m_VertexBuffer)->Invalidate();
        spec.Layout = { { ShaderDataType::Float3, "a_Position" } };

        spec.DebugName   = "test temp";
        spec.Framebuffer = m_framebuffer;
        spec.Shader      = m_Shader;
        m_Pipeline       = Pipeline::Create( spec );
        m_Pipeline->Invalidate();
    }

} // namespace Desert::Graphic::API::Vulkan