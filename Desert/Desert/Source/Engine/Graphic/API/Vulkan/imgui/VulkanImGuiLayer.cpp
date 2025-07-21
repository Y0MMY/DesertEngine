#include <Engine/Graphic/API/Vulkan/imgui/VulkanImGuiLayer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Core/EngineContext.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>

#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanFramebuffer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderCommandBuffer.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanRenderer.hpp>
#include <Engine/Graphic/RendererAPI.hpp>

#ifndef IMGUI_IMPL_API
#define IMGUI_IMPL_API
#endif

#include <ImGui/backends/imgui_impl_glfw.h>
#include <ImGui/backends/imgui_impl_vulkan.h>

namespace Desert::Graphic::API::Vulkan::ImGui
{
    Common::BoolResult VulkanImGui::OnAttach()
    {
        const auto& device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();
        const auto  window = Common::CommonContext::GetInstance().GetCurrentPointerToGLFWwinodw();

        VkDescriptorPoolSize       pool_sizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 100 },
                                                    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 },
                                                    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100 },
                                                    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 },
                                                    { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100 },
                                                    { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100 },
                                                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 },
                                                    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 },
                                                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 100 },
                                                    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 100 },
                                                    { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100 } };
        VkDescriptorPoolCreateInfo pool_info    = {};
        pool_info.sType                         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags                         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets                       = 100 * IM_ARRAYSIZE( pool_sizes );
        pool_info.poolSizeCount                 = (uint32_t)IM_ARRAYSIZE( pool_sizes );
        pool_info.pPoolSizes                    = pool_sizes;
        VK_CHECK_RESULT( vkCreateDescriptorPool( device, &pool_info, nullptr, &m_ImguiPool ) );

        const auto& context   = static_cast<VulkanContext*>( Renderer::GetInstance().GetRendererContext().get() );
        const auto& swapchain = context->GetVulkanSwapChain();

        // Setup Platform/Renderer bindings
        ImGui_ImplGlfw_InitForVulkan( window, true );
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance                  = context->GetVulkanInstance();
        init_info.PhysicalDevice =
             VulkanLogicalDevice::GetInstance().GetPhysicalDevice()->GetVulkanPhysicalDevice();
        init_info.Device         = device;
        init_info.Queue          = VulkanLogicalDevice::GetInstance().GetGraphicsQueue();
        init_info.PipelineCache  = nullptr;
        init_info.DescriptorPool = m_ImguiPool;
        init_info.Allocator      = nullptr;
        init_info.MinImageCount  = 3;
        init_info.ImageCount     = 3;
        init_info.MSAASamples    = VK_SAMPLE_COUNT_1_BIT;

        ImGui_ImplVulkan_Init( &init_info, swapchain->GetRenderPass() );

        {
            ImGuiIO& io = ::ImGui::GetIO();

            ImFont* mainFont = io.Fonts->AddFontFromFileTTF( "Resources/Fonts/Roboto-Black.ttf", 16.0f, nullptr,
                                                             io.Fonts->GetGlyphRangesCyrillic() );

            if ( !mainFont )
            {
                mainFont = io.Fonts->AddFontDefault();
            }
            io.FontDefault = mainFont;

            const auto commandBuffer =
                 CommandBufferAllocator::GetInstance().RT_AllocateCommandBufferGraphic( true );
            ImGui_ImplVulkan_CreateFontsTexture( commandBuffer.GetValue() );
            CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( commandBuffer.GetValue() );

            VK_CHECK_RESULT( vkDeviceWaitIdle( device ) );
            ImGui_ImplVulkan_DestroyFontUploadObjects();
        }

        return BOOLSUCCESS;
    }

    Common::BoolResult VulkanImGui::OnDetach()
    {
        const auto& device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();
        vkDeviceWaitIdle( device );

        ImGui_ImplVulkan_Shutdown(); // Уничтожаем Vulkan-бэкенд
        ImGui_ImplGlfw_Shutdown();   // Уничтожаем GLFW-бэкенд
        ::ImGui::DestroyContext();   // Уничтожаем контекст ImGui

        if ( m_ImguiPool != VK_NULL_HANDLE )
        {
            vkDestroyDescriptorPool( device, m_ImguiPool, nullptr );
            m_ImguiPool = VK_NULL_HANDLE;
        }

        return BOOLSUCCESS;
    }

    Common::BoolResult VulkanImGui::OnUpdate( const Common::Timestep& )
    {
        VulkanRenderCommandBuffer::GetInstance().RegisterUserCommand( []() { ::ImGui::ShowDemoWindow(); } );

        return BOOLSUCCESS;
    }

    void VulkanImGui::Begin()
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ::ImGui::NewFrame();
    }

    void VulkanImGui::End()
    {
        ::ImGui::Render();

        const auto& context   = static_cast<VulkanContext*>( Renderer::GetInstance().GetRendererContext().get() );
        const auto& swapChain = context->GetVulkanSwapChain();

        VkClearValue clearValues[2];
        clearValues[0].color        = { { 0.1f, 0.1f, 0.1f, 1.0f } };
        clearValues[1].depthStencil = { 1.0f, 0 };

        uint32_t width  = swapChain->GetWidth();
        uint32_t height = swapChain->GetHeight();

        uint32_t commandBufferIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        VkCommandBuffer drawCommandBuffer =
             CommandBufferAllocator::GetInstance().RT_AllocateCommandBufferGraphic( true ).GetValue();

        VkRenderPassBeginInfo renderPassBeginInfo    = {};
        renderPassBeginInfo.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass               = swapChain->GetRenderPass();
        renderPassBeginInfo.framebuffer              = swapChain->GetVKFramebuffers()[commandBufferIndex];
        renderPassBeginInfo.renderArea.extent.width  = width;
        renderPassBeginInfo.renderArea.extent.height = height;
        renderPassBeginInfo.clearValueCount          = 2;
        renderPassBeginInfo.pClearValues             = clearValues;

        vkCmdBeginRenderPass( drawCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE );

        VkViewport viewport = {};
        viewport.x          = 0.0f;
        viewport.y          = 0.0f;
        viewport.width      = (float)width;
        viewport.height     = (float)height;
        viewport.minDepth   = 0.0f;
        viewport.maxDepth   = 1.0f;
        vkCmdSetViewport( drawCommandBuffer, 0, 1, &viewport );

        VkRect2D scissor      = {};
        scissor.extent.width  = width;
        scissor.extent.height = height;
        scissor.offset.x      = 0;
        scissor.offset.y      = 0;
        vkCmdSetScissor( drawCommandBuffer, 0, 1, &scissor );

        ImDrawData* main_draw_data = ::ImGui::GetDrawData();
        ImGui_ImplVulkan_RenderDrawData( main_draw_data, drawCommandBuffer );

        vkCmdEndRenderPass( drawCommandBuffer );

        CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( drawCommandBuffer );

        ImGuiIO& io = ::ImGui::GetIO();

        (void)io;
        // Update and Render additional Platform Windows
        if ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
        {
            ::ImGui::UpdatePlatformWindows();
            ::ImGui::RenderPlatformWindowsDefault();
        }
    }

} // namespace Desert::Graphic::API::Vulkan::ImGui