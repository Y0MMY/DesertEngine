#include "UICanvasRenderer.hpp"

#include <Engine/ECS/Components.hpp>
#include <Engine/Assets/Common.hpp>
#include <Engine/Graphic/Texture.hpp>
#include <Engine/Graphic/Image.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <ImGui/imgui.h>

#include <algorithm>
#include <cfloat>

namespace Desert::UI
{
    namespace
    {
        namespace ImGui = ::ImGui;

        ImU32 Col( const glm::vec3& c, float a = 1.0f )
        {
            return ImGui::ColorConvertFloat4ToU32( ImVec4( c.r, c.g, c.b, a ) );
        }

        // Engine-side sprite resolution: AssetHandle -> runtime GPU texture -> Image2D. Returns a non-owning
        // shared_ptr (the image is owned by the image service; UICacheTexture keys ImGui descriptors by
        // VkImageView, so re-wrapping each frame is safe). nullptr when unset / unresolvable.
        std::shared_ptr<Graphic::Image2D> ResolveSpriteImage( const Assets::AssetHandle& handle )
        {
            auto* service = Runtime::ResourceRegistry::GetTextureService();
            if ( !service )
                return nullptr;
            auto* tex = service->Get( handle );
            if ( !tex )
                return nullptr;
            auto* imgService = Runtime::ResourceRegistry::GetImageService();
            if ( !imgService )
                return nullptr;
            auto* img = static_cast<Graphic::Image2D*>( imgService->Resolve( tex->GetImageHandle() ) );
            if ( !img )
                return nullptr;
            return std::shared_ptr<Graphic::Image2D>( img, []( Graphic::Image2D* ) {} );
        }

        // Draw a filled UI box: a sprite (tinted by `col`) when one is bound + resolvable, else a flat colour.
        void DrawBox( ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, const glm::vec3& col, float alpha,
                      float rounding, const Assets::AssetHandle& sprite, const SpriteResolver& resolver )
        {
            const void* tex = nullptr;
            if ( resolver )
                if ( auto img = ResolveSpriteImage( sprite ) )
                    tex = resolver( img );
            if ( tex )
                dl->AddImage( (ImTextureID)tex, mn, mx, ImVec2( 0.0f, 0.0f ), ImVec2( 1.0f, 1.0f ),
                              Col( col, alpha ) );
            else
                dl->AddRectFilled( mn, mx, Col( col, alpha ), rounding );
        }

        // Maps the canvas to the viewport per its scale mode (see UICanvasScaleMode). Returns the canvas root
        // rect (screen px) + a uniform scale applied to every element's offsets/min-size/font — so Stretch is
        // 1:1 (anchors drive layout, no resize zoom), ScaleWithScreen scales the whole design from the
        // reference resolution, and Letterbox fits + centres it. SHARED by draw + hit-test so they agree.
        struct CanvasFit
        {
            Rect  Root;
            float Scale;
        };
        CanvasFit ResolveCanvas( const ECS::UICanvasData& d, const Rect& viewportPx )
        {
            switch ( d.ScaleMode )
            {
                case ECS::UICanvasScaleMode::ScaleWithScreen:
                {
                    const float sx = d.ReferenceWidth > 0.0f ? viewportPx.W / d.ReferenceWidth : 1.0f;
                    const float sy = d.ReferenceHeight > 0.0f ? viewportPx.H / d.ReferenceHeight : 1.0f;
                    const float m  = std::clamp( d.MatchWidthHeight, 0.0f, 1.0f );
                    return { viewportPx, sx * ( 1.0f - m ) + sy * m };
                }
                case ECS::UICanvasScaleMode::Letterbox:
                {
                    const Rect fit = CanvasRect( d.ReferenceWidth, d.ReferenceHeight, viewportPx.W, viewportPx.H );
                    const float scale = d.ReferenceWidth > 0.0f ? fit.W / d.ReferenceWidth : 1.0f;
                    return { Rect{ viewportPx.X + fit.X, viewportPx.Y + fit.Y, fit.W, fit.H }, scale };
                }
                case ECS::UICanvasScaleMode::Stretch:
                default:
                    return { viewportPx, 1.0f }; // canvas == viewport, 1:1 px
            }
        }

        // Recursively resolve + draw one element and its children. `parent` is the parent rect in SCREEN
        // pixels; `scale` maps design px -> screen px (font sizing). A hovered+released button writes its
        // OnClickMessage to *outClicked. This is the SINGLE draw path shared by the editor preview and the
        // in-game runtime, so a canvas looks identical in both.
        void DrawElement( entt::registry& reg, entt::entity e, const Rect& parent, float scale, ImDrawList* dl,
                          bool interactive, std::string* outClicked, const SpriteResolver& sprites )
        {
            Rect rect = parent;
            if ( reg.has<ECS::UILayoutComponent>( e ) )
            {
                const auto& L = reg.get<ECS::UILayoutComponent>( e ).Data;
                rect          = ResolveRect( L.AnchorMin, L.AnchorMax, L.OffsetMin * scale, L.OffsetMax * scale,
                                             L.CustomMinimumSize * scale, parent );

                const ImVec2 mn( rect.X, rect.Y );
                const ImVec2 mx( rect.X + rect.W, rect.Y + rect.H );

                if ( reg.has<ECS::UIButtonComponent>( e ) )
                {
                    const auto&  b     = reg.get<ECS::UIButtonComponent>( e ).Data;
                    const ImVec2 m     = ImGui::GetMousePos();
                    const bool   hover = interactive && m.x >= mn.x && m.x <= mx.x && m.y >= mn.y && m.y <= mx.y;
                    const bool   down  = hover && ImGui::IsMouseDown( ImGuiMouseButton_Left );
                    const glm::vec3 c  = down ? b.PressedColor : ( hover ? b.HoverColor : b.NormalColor );
                    DrawBox( dl, mn, mx, c, 1.0f, 6.0f, b.Sprite, sprites );

                    if ( hover && outClicked && ImGui::IsMouseReleased( ImGuiMouseButton_Left ) )
                        *outClicked = b.OnClickMessage;
                }
                else if ( reg.has<ECS::UIPanelComponent>( e ) )
                {
                    const auto& p = reg.get<ECS::UIPanelComponent>( e ).Data;
                    DrawBox( dl, mn, mx, p.Color, p.Opacity, p.CornerRadius, p.Sprite, sprites );
                }

                if ( reg.has<ECS::UITextComponent2D>( e ) )
                {
                    const auto&  t  = reg.get<ECS::UITextComponent2D>( e ).Data;
                    const float  fs = t.FontSize * scale;
                    const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA( fs, FLT_MAX, 0.0f, t.Text.c_str() );
                    float        tx = mn.x + 6.0f;
                    if ( t.Align == ECS::UITextAlign::Center )
                        tx = mn.x + ( rect.W - ts.x ) * 0.5f;
                    else if ( t.Align == ECS::UITextAlign::Right )
                        tx = mx.x - ts.x - 6.0f;
                    const float ty = mn.y + ( rect.H - ts.y ) * 0.5f;
                    dl->AddText( ImGui::GetFont(), fs, ImVec2( tx, ty ), Col( t.Color, 1.0f ), t.Text.c_str() );
                }
            }

            if ( reg.has<ECS::RelationshipComponent>( e ) )
            {
                // Clip Contents (opt-in per element, like Unity's RectMask2D): children are clipped to this
                // element's rect. Overflow is allowed by default — a panel CAN extend past its parent/canvas
                // unless you enable this. Intersects the current clip so nested masks compose.
                const bool clip = reg.has<ECS::UILayoutComponent>( e ) &&
                                  reg.get<ECS::UILayoutComponent>( e ).Data.ClipContents;
                if ( clip )
                    dl->PushClipRect( ImVec2( rect.X, rect.Y ), ImVec2( rect.X + rect.W, rect.Y + rect.H ), true );
                for ( auto c : reg.get<ECS::RelationshipComponent>( e ).Children )
                    if ( reg.valid( c ) )
                        DrawElement( reg, c, rect, scale, dl, interactive, outClicked, sprites );
                if ( clip )
                    dl->PopClipRect();
            }
        }

        // Resolves the canvas root rect (letterboxed into viewportPx) exactly like RenderCanvas. false if the
        // scene has no visible canvas. Shared by PickElement / GetElementRect so hit-testing matches drawing.
        bool CanvasRootRect( entt::registry& reg, const Rect& viewportPx, entt::entity& canvasOut, Rect& rectOut,
                             float& scaleOut )
        {
            auto canvasView = reg.view<ECS::UICanvasComponent>();
            if ( canvasView.begin() == canvasView.end() )
                return false;

            const entt::entity canvasEntity = *canvasView.begin();
            const auto&        canvasData   = reg.get<ECS::UICanvasComponent>( canvasEntity ).Data;
            if ( !canvasData.Visible )
                return false;

            const CanvasFit fit = ResolveCanvas( canvasData, viewportPx );
            rectOut             = fit.Root;
            scaleOut            = fit.Scale;
            canvasOut           = canvasEntity;
            return true;
        }

        void PickRecurse( entt::registry& reg, entt::entity e, const Rect& parent, float scale, const glm::vec2& p,
                          entt::entity& hit )
        {
            Rect rect = parent;
            if ( reg.has<ECS::UILayoutComponent>( e ) )
            {
                const auto& L = reg.get<ECS::UILayoutComponent>( e ).Data;
                rect          = ResolveRect( L.AnchorMin, L.AnchorMax, L.OffsetMin * scale, L.OffsetMax * scale,
                                             L.CustomMinimumSize * scale, parent );
                const bool drawable = reg.has<ECS::UIPanelComponent>( e ) ||
                                      reg.has<ECS::UITextComponent2D>( e ) || reg.has<ECS::UIButtonComponent>( e );
                if ( drawable && p.x >= rect.X && p.x <= rect.X + rect.W && p.y >= rect.Y &&
                     p.y <= rect.Y + rect.H )
                    hit = e; // later (deeper / drawn-on-top) hits overwrite, matching draw order
            }
            if ( reg.has<ECS::RelationshipComponent>( e ) )
                for ( auto c : reg.get<ECS::RelationshipComponent>( e ).Children )
                    if ( reg.valid( c ) )
                        PickRecurse( reg, c, rect, scale, p, hit );
        }

        void RectRecurse( entt::registry& reg, entt::entity e, const Rect& parent, float scale,
                          entt::entity target, Rect& out, bool& found )
        {
            Rect rect = parent;
            if ( reg.has<ECS::UILayoutComponent>( e ) )
            {
                const auto& L = reg.get<ECS::UILayoutComponent>( e ).Data;
                rect          = ResolveRect( L.AnchorMin, L.AnchorMax, L.OffsetMin * scale, L.OffsetMax * scale,
                                             L.CustomMinimumSize * scale, parent );
            }
            if ( e == target )
            {
                out   = rect;
                found = true;
                return;
            }
            if ( reg.has<ECS::RelationshipComponent>( e ) )
                for ( auto c : reg.get<ECS::RelationshipComponent>( e ).Children )
                    if ( reg.valid( c ) && !found )
                        RectRecurse( reg, c, rect, scale, target, out, found );
        }
    } // namespace

    bool RenderCanvas( entt::registry& reg, ImDrawList* dl, const Rect& viewportPx, bool interactive,
                       std::string* outClicked, const SpriteResolver& sprites )
    {
        auto canvasView = reg.view<ECS::UICanvasComponent>();
        if ( canvasView.begin() == canvasView.end() )
            return false;

        const entt::entity canvasEntity = *canvasView.begin();
        const auto&        canvasData   = reg.get<ECS::UICanvasComponent>( canvasEntity ).Data;
        if ( !canvasData.Visible )
            return false;

        // Map the canvas to the viewport per its scale mode (Stretch fills 1:1, ScaleWithScreen scales the
        // design, Letterbox fits + centres). Offsets/fonts are multiplied by scale inside DrawElement.
        const CanvasFit fit        = ResolveCanvas( canvasData, viewportPx );
        const Rect      canvasRect = fit.Root;
        const float     scale      = fit.Scale;

        if ( reg.has<ECS::RelationshipComponent>( canvasEntity ) )
            for ( auto c : reg.get<ECS::RelationshipComponent>( canvasEntity ).Children )
                if ( reg.valid( c ) )
                    DrawElement( reg, c, canvasRect, scale, dl, interactive, outClicked, sprites );

        return true;
    }

    entt::entity PickElement( entt::registry& reg, const glm::vec2& pointPx, const Rect& viewportPx )
    {
        entt::entity canvas;
        Rect         canvasRect;
        float        scale = 1.0f;
        if ( !CanvasRootRect( reg, viewportPx, canvas, canvasRect, scale ) )
            return entt::null;

        entt::entity hit = entt::null;
        if ( reg.has<ECS::RelationshipComponent>( canvas ) )
            for ( auto c : reg.get<ECS::RelationshipComponent>( canvas ).Children )
                if ( reg.valid( c ) )
                    PickRecurse( reg, c, canvasRect, scale, pointPx, hit );
        return hit;
    }

    bool GetElementRect( entt::registry& reg, entt::entity target, const Rect& viewportPx, Rect& out )
    {
        entt::entity canvas;
        Rect         canvasRect;
        float        scale = 1.0f;
        if ( !CanvasRootRect( reg, viewportPx, canvas, canvasRect, scale ) )
            return false;

        if ( target == canvas )
        {
            out = canvasRect;
            return true;
        }
        bool found = false;
        if ( reg.has<ECS::RelationshipComponent>( canvas ) )
            for ( auto c : reg.get<ECS::RelationshipComponent>( canvas ).Children )
                if ( reg.valid( c ) && !found )
                    RectRecurse( reg, c, canvasRect, scale, target, out, found );
        return found;
    }

    float CanvasScale( entt::registry& reg, const Rect& viewportPx )
    {
        entt::entity canvas;
        Rect         canvasRect;
        float        scale = 1.0f;
        CanvasRootRect( reg, viewportPx, canvas, canvasRect, scale );
        return scale;
    }
} // namespace Desert::UI
