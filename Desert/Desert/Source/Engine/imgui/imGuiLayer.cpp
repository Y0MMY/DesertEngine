#include <Engine/imgui/ImGuiLayer.hpp>

#include <Engine/Graphic/RendererAPI.hpp>
#include <Engine/Graphic/API/Vulkan/imgui/VulkanImGuiLayer.hpp>

namespace Desert::ImGui
{

    std::shared_ptr<Desert::ImGui::ImGuiLayer> ImGuiLayer::Create()
    {
        switch ( Graphic::RendererAPI::GetAPIType() )
        {
            case Graphic::RendererAPIType::None:
                return nullptr;
            case Graphic::RendererAPIType::Vulkan:
                return std::make_shared<Graphic::API::Vulkan::ImGui::VulkanImGui>();
        }
        DESERT_VERIFY( false, "Unknown RenderingAPI" );
        return nullptr;
    }

} // namespace Desert::ImGui