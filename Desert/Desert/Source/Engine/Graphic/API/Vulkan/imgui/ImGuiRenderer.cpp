#include <Engine/Graphic/API/Vulkan/imgui/ImGuiRenderer.hpp>

#include <ImGui/backends/imgui_impl_glfw.h>
#include <ImGui/backends/imgui_impl_vulkan.h>

namespace Desert::Graphic::API::Vulkan::ImGui
{

    [[maybe_unused]] void VulkanImGuiRenderer::RenderImGui(VkCommandBuffer commandbuffer)
    {
        ::ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData( ::ImGui::GetDrawData(), commandbuffer, 0 );
    }

} // namespace Desert::Graphic::API::Vulkan::ImGui
