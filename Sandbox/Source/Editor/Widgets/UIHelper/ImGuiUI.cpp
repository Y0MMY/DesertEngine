#include "ImGuiUI.hpp"

#include <Engine/Desert.hpp>

namespace Desert::Editor::UI
{
    static std::shared_ptr<Graphic::UICacheTexture> s_CacherTexture;


    namespace ImGui = ::ImGui;
    void UIHelper::Init()
    {
        s_CacherTexture = Graphic::UICacheTexture::Create();
    }

    void UIHelper::Image( const std::shared_ptr<Graphic::Image2D>& image, const ImVec2& size, const ImVec2& uv0,
                          const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& border_col )
    {
        if ( !image )
            return;

        const auto* id = s_CacherTexture->AddTextureCache( image );

        ::ImGui::Image( (ImTextureID)id, size, uv0, uv1, tint_col, border_col );
    }
} // namespace Desert::Editor::UI