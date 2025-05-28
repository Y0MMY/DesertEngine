#pragma once

#include <Engine/imgui/ImGuiLayer.hpp>
#include <Common/Core/Timestep.hpp>

#include <vulkan/vulkan.h>

namespace Desert::Graphic::API::Vulkan::ImGui
{
    class VulkanImGui : public Desert::ImGui::ImGuiLayer
    {
    public:
        virtual Common::BoolResult OnAttach() override;
        virtual Common::BoolResult OnDetach() override;
        virtual Common::BoolResult OnUpdate( Common::Timestep ts ) override;
        virtual void               Begin() override;
        virtual void               Process( const std::function<void()>& func ) override;
        virtual void               End() override;

        virtual Common::BoolResult OnImGuiRender() override
        {
            return BOOLSUCCESS;
        }

    private:
        VkDescriptorPool m_ImguiPool;
    };
} // namespace Desert::Graphic::API::Vulkan::ImGui