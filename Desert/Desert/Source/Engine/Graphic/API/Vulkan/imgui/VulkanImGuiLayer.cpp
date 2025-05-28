#include <Engine/Graphic/API/Vulkan/imgui/VulkanImGuiLayer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Core/EngineContext.h>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>

#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanFramebuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderCommandBuffer.hpp>

#ifndef IMGUI_IMPL_API
#define IMGUI_IMPL_API
#endif

#include <ImGui/backends/imgui_impl_glfw.h>
#include <ImGui/backends/imgui_impl_vulkan.h>

namespace Desert::Graphic::API::Vulkan::ImGui
{
    static VkRenderPass s_RenderpassImgui;

    VkRenderPass CreateImGuiRenderPass( VkDevice device, VkFormat swapchainFormat )
    {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format         = swapchainFormat;
        colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments    = &colorAttachmentRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass    = 0;
        dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments    = &colorAttachment;
        renderPassInfo.subpassCount    = 1;
        renderPassInfo.pSubpasses      = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies   = &dependency;

        VkRenderPass imguiRenderPass;
        if ( vkCreateRenderPass( device, &renderPassInfo, nullptr, &imguiRenderPass ) != VK_SUCCESS )
        {
        }

        return imguiRenderPass;
    }

    Common::BoolResult VulkanImGui::OnAttach()
    {
        //  1: create descriptor pool for IMGUI
        // the size of the pool is very oversize, but it's copied from imgui demo itself.
        VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
                                              { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
                                              { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
                                              { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
                                              { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
                                              { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
                                              { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
                                              { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
                                              { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
                                              { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
                                              { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags                      = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets                    = 1000;
        pool_info.poolSizeCount              = std::size( pool_sizes );
        pool_info.pPoolSizes                 = pool_sizes;

        const auto& device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

        VK_CHECK_RESULT( vkCreateDescriptorPool( device, &pool_info, nullptr, &m_ImguiPool ) );

        // 2: initialize imgui library

        // this initializes the core structures of imgui
        ::ImGui::CreateContext();

        LOG_TRACE( "Attaching VulkanImGui" );

        ImGui_ImplGlfw_InitForVulkan( EngineContext::GetInstance().GetCurrentPointerToGLFWwinodw(), true );

        const auto& context = std::static_pointer_cast<Graphic::API::Vulkan::VulkanContext>(
             Renderer::GetInstance().GetRendererContext() );

        const auto& swapchain = context->GetVulkanSwapChain();

        // this initializes imgui for Vulkan
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance                  = context->GetVulkanInstance();
        init_info.PhysicalDevice =
             VulkanLogicalDevice::GetInstance().GetPhysicalDevice()->GetVulkanPhysicalDevice();
        init_info.Device         = device;
        init_info.Queue          = VulkanLogicalDevice::GetInstance().GetGraphicsQueue();
        init_info.DescriptorPool = m_ImguiPool;
        init_info.MinImageCount  = 3;
        init_info.ImageCount     = 3;
        init_info.MSAASamples    = VK_SAMPLE_COUNT_1_BIT;

        ImGui_ImplVulkan_Init( &init_info, swapchain->GetRenderPass() );

        auto commandBuffer = CommandBufferAllocator::GetInstance().RT_AllocateCommandBufferGraphic( true );
        ImGui_ImplVulkan_CreateFontsTexture( commandBuffer.GetValue() );
        CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( commandBuffer.GetValue() );

        // Clear font textures from cpu data
        ImGui_ImplVulkan_DestroyFontUploadObjects();

        s_RenderpassImgui = CreateImGuiRenderPass( device, swapchain->GetColorFormat() );

        return BOOLSUCCESS;
    }

    Common::BoolResult VulkanImGui::OnDetach()
    {
        const auto& device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

        vkDeviceWaitIdle( device );

        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ::ImGui::DestroyContext();

        vkDestroyDescriptorPool( device, m_ImguiPool, nullptr );

        return BOOLSUCCESS;
    }

    Common::BoolResult VulkanImGui::OnUpdate( Common::Timestep ts )
    {
        VulkanRenderCommandBuffer::GetInstance().RegisterUserCommand( []() { ::ImGui::ShowDemoWindow(); } );

        return BOOLSUCCESS;
    }

    void VulkanImGui::Process( const std::function<void()>& func )
    {
        VulkanRenderCommandBuffer::GetInstance().RegisterUserCommand( func );
    }

    void VulkanImGui::Begin()
    {
        VulkanRenderCommandBuffer::GetInstance().RegisterUserCommand(
             []()
             {
                 ImGui_ImplVulkan_NewFrame();
                 ImGui_ImplGlfw_NewFrame();
                 ::ImGui::NewFrame();
             } );
    }

    void VulkanImGui::End()
    {
        VulkanRenderCommandBuffer::GetInstance().RegisterUserCommand(
             []()
             {
                 ::ImGui::Render();

                 VkCommandBuffer       commandBuffer = VulkanRenderCommandBuffer::GetInstance().GetCommandBuffer();
                 VkRenderPassBeginInfo renderPassInfo{};
                 renderPassInfo.sType      = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                 renderPassInfo.renderPass = s_RenderpassImgui;

                 uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

                 const auto& context = std::static_pointer_cast<Graphic::API::Vulkan::VulkanContext>(
                      Renderer::GetInstance().GetRendererContext() );

                 const auto& swapchain = context->GetVulkanSwapChain();

                 uint32_t windowWidth  = EngineContext::GetInstance().GetCurrentWindowWidth();
                 uint32_t windowHeight = EngineContext::GetInstance().GetCurrentWindowHeight();

                 renderPassInfo.framebuffer       = swapchain->GetVKFramebuffers()[frameIndex];
                 renderPassInfo.renderArea.offset = { 0, 0 };
                 renderPassInfo.renderArea.extent = { windowWidth, windowHeight };
                 renderPassInfo.clearValueCount   = 0;
                 renderPassInfo.pClearValues      = nullptr;

                 vkCmdBeginRenderPass( commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE );

                 ImGui_ImplVulkan_RenderDrawData( ::ImGui::GetDrawData(), commandBuffer, 0 );

                 vkCmdEndRenderPass( commandBuffer );

                 ::ImGui::UpdatePlatformWindows();
             } );
    }
} // namespace Desert::Graphic::API::Vulkan::ImGui