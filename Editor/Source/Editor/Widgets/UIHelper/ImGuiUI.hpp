#pragma once

#include <ImGui/imgui.h>

#include <Engine/Desert.hpp>

namespace Desert::Editor::UI
{
    class UIHelper
    {
    public:
        UIHelper() = default;

        void Init();

        void Image( const std::shared_ptr<Graphic::Image2D>& image, const ImVec2& size,
                    const ImVec2& uv0 = ImVec2( 0, 0 ), const ImVec2& uv1 = ImVec2( 1, 1 ),
                    const ImVec4& tint_col   = ImVec4( 1, 1, 1, 1 ),
                    const ImVec4& border_col = ImVec4( 0, 0, 0, 0 ) );

        // Interactive (clickable + drag-droppable) image. Returns true on click. Used for asset-grid
        // thumbnails that must act as drag sources (plain Image() is not an interactive item).
        bool ImageButton( const char* strId, const std::shared_ptr<Graphic::Image2D>& image,
                          const ImVec2& size );

    private:
        std::unique_ptr<Graphic::UICacheTexture> m_CacherTexture;
    };
} // namespace Desert::Editor::UI