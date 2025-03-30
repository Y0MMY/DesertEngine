#pragma once

#include <Engine/imgui/ImGuiLayer.hpp>
#include <Common/Core/Timestep.hpp>

#include <vulkan/vulkan.h>

namespace Desert::Graphic::API::Vulkan::ImGui
{
    class VulkanImGui : public Desert::ImGui::ImGuiLayer
    {
    public:
        virtual void OnAttach() override;
        virtual void OnDetach() override;
        virtual void OnUpdate( Common::Timestep ts ) override;
        virtual void Begin() override;
        virtual void End() override;

        virtual void OnImGuiRender() override
        {
        }
    private:
        VkDescriptorPool m_ImguiPool;

    };
} // namespace Desert::Graphic::API::Vulkan::ImGui