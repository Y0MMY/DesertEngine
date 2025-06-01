#include <Engine/Graphic/UICacheTexture.hpp>

#include <Engine/imgui/UICacheTextureImGui.hpp>

namespace Desert::Graphic
{

    std::shared_ptr<UICacheTexture> UICacheTexture::Create()
    {
        return std::make_shared<ImGui::UICacheTextureImGui>();
    }

} // namespace Desert::Graphic