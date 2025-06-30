#include <Engine/Graphic/UICacheTexture.hpp>

#include <Engine/imgui/UICacheTextureImGui.hpp>

namespace Desert::Graphic
{

    std::unique_ptr<UICacheTexture> UICacheTexture::Create()
    {
        return std::make_unique<ImGui::UICacheTextureImGui>();
    }

} // namespace Desert::Graphic