#pragma once

#include <Engine/imgui/ImGuiLayer.hpp>
#include <Common/Core/Timestep.hpp>

#include <vulkan/vulkan.h>

namespace Desert::Graphic::API::Vulkan::ImGui
{
    class VulkanImGui : public Desert::ImGui::ImGuiLayer
    {
    public:
        virtual Common::BoolResultStr OnAttach() override;
        virtual Common::BoolResultStr OnDetach() override;
        virtual Common::BoolResultStr OnUpdate( const Common::Timestep& ts ) override;
        virtual void               Begin() override;
        virtual void               End() override;

        virtual Common::BoolResultStr OnImGuiRender() override
        {
            return BOOLSUCCESS;
        }

    private:
        VkDescriptorPool m_ImguiPool;
    };
} // namespace Desert::Graphic::API::Vulkan::ImGui