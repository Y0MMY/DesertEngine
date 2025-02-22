#pragma once

#include <vulkan/vulkan.h>

#include <Common\Core\Singleton.hpp>

namespace Desert::Graphic::API::Vulkan::ImGui
{
    class VulkanImGuiRenderer : public Common::Singleton<VulkanImGuiRenderer>
    {
    public:
        [[maybe_unused]]void RenderImGui(VkCommandBuffer commandbuffer);

    private:
    };

} // namespace Desert::Graphic::API::Vulkan::ImGui