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

        bool HandleSet( const Assets::AssetHandle& h )
        {
            return static_cast<uint64_t>( h ) != 0;
        }

        // Draw a filled UI box: a sprite (tinted by `col`) when one is bound + resolvable, else a flat colour.
        // srcBorder (L,T,R,B in SOURCE pixels) enables 9-slice: corners stay unstretched (x canvas scale),
        // edges/centre stretch — so image buttons/panels resize without distorting their borders.
        void DrawBox( ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, const glm::vec3& col, float alpha,
                      float rounding, const Assets::AssetHandle& sprite, const SpriteResolver& resolver,
                      const glm::vec4& srcBorder = glm::vec4( 0.0f ), float scale = 1.0f )
        {
            std::shared_ptr<Graphic::Image2D> img;
            const void*                       tex = nullptr;
            if ( resolver )
                if ( ( img = ResolveSpriteImage( sprite ) ) )
                    tex = resolver( img );
            if ( !tex )
            {
                dl->AddRectFilled( mn, mx, Col( col, alpha ), rounding );
                return;
            }

            const ImU32 tint = Col( col, alpha );
            const float tw   = img ? static_cast<float>( img->GetWidth() ) : 0.0f;
            const float th   = img ? static_cast<float>( img->GetHeight() ) : 0.0f;
            const bool  nine =
                 tw > 0.0f && th > 0.0f &&
                 ( srcBorder.x > 0.0f || srcBorder.y > 0.0f || srcBorder.z > 0.0f || srcBorder.w > 0.0f );
            if ( !nine )
            {
                dl->AddImage( (ImTextureID)tex, mn, mx, ImVec2( 0.0f, 0.0f ), ImVec2( 1.0f, 1.0f ), tint );
                return;
            }

            const float hw = ( mx.x - mn.x ) * 0.5f, hh = ( mx.y - mn.y ) * 0.5f;
            const float pl = std::min( srcBorder.x * scale, hw ), pt = std::min( srcBorder.y * scale, hh );
            const float pr = std::min( srcBorder.z * scale, hw ), pb = std::min( srcBorder.w * scale, hh );
            const float xs[4] = { mn.x, mn.x + pl, mx.x - pr, mx.x };
            const float ys[4] = { mn.y, mn.y + pt, mx.y - pb, mx.y };
            const float us[4] = { 0.0f, srcBorder.x / tw, 1.0f - srcBorder.z / tw, 1.0f };
            const float vs[4] = { 0.0f, srcBorder.y / th, 1.0f - srcBorder.w / th, 1.0f };
            for ( int r = 0; r < 3; ++r )
                for ( int c = 0; c < 3; ++c )
                    dl->AddImage( (ImTextureID)tex, ImVec2( xs[c], ys[r] ), ImVec2( xs[c + 1], ys[r + 1] ),
                                  ImVec2( us[c], vs[r] ), ImVec2( us[c + 1], vs[r + 1] ), tint );
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

                    // Image can change with state (hover / press) — falls back to the normal Sprite if a
                    // per-state one isn't set. This is the "hover -> change image" without any graph.
                    Assets::AssetHandle spr = b.Sprite;
                    if ( down && HandleSet( b.PressedSprite ) )
                        spr = b.PressedSprite;
                    else if ( hover && HandleSet( b.HoverSprite ) )
                        spr = b.HoverSprite;
                    DrawBox( dl, mn, mx, c, 1.0f, 6.0f, spr, sprites, b.SpriteBorder, scale );

                    if ( hover && outClicked && ImGui::IsMouseReleased( ImGuiMouseButton_Left ) )
                    {
                        // Encode the structured action into the click message the runtime dispatches.
                        switch ( b.Action )
                        {
                            case ECS::UIButtonAction::LoadScene:
                                *outClicked = "scene:" + b.OnClickMessage;
                                break;
                            case ECS::UIButtonAction::QuitGame:
                                *outClicked = "quit";
                                break;
                            case ECS::UIButtonAction::OpenURL:
                                *outClicked = "url:" + b.OnClickMessage;
                                break;
                            case ECS::UIButtonAction::SendMessage:
                                *outClicked = b.OnClickMessage;
                                break;
                            case ECS::UIButtonAction::None:
                            default:
                                break;
                        }
                    }
                }
                else if ( reg.has<ECS::UIPanelComponent>( e ) )
                {
                    const auto& p = reg.get<ECS::UIPanelComponent>( e ).Data;

                    if ( p.Glow && p.GlowSize > 0.0f )
                    {
                        const int   layers = 6;
                        const float gs     = p.GlowSize * scale;
                        for ( int i = 0; i < layers; ++i ) // large faint -> small; they overlap into a soft glow
                        {
                            const float ex = gs * ( 1.0f - static_cast<float>( i ) / layers );
                            dl->AddRectFilled( ImVec2( mn.x - ex, mn.y - ex ), ImVec2( mx.x + ex, mx.y + ex ),
                                               Col( p.GlowColor, 0.10f * p.Opacity ), p.CornerRadius + ex );
                        }
                    }

                    if ( p.Shadow )
                        dl->AddRectFilled(
                             ImVec2( mn.x + p.ShadowOffset.x * scale, mn.y + p.ShadowOffset.y * scale ),
                             ImVec2( mx.x + p.ShadowOffset.x * scale, mx.y + p.ShadowOffset.y * scale ),
                             Col( p.ShadowColor, p.Opacity ), p.CornerRadius );

                    if ( p.UseGradient && !HandleSet( p.Sprite ) )
                    {
                        const ImU32 top = Col( p.Color, p.Opacity );
                        const ImU32 bot = Col( p.GradientColor, p.Opacity );
                        dl->AddRectFilledMultiColor( mn, mx, top, top, bot,
                                                     bot ); // vertical gradient (no rounding)
                    }
                    else
                    {
                        DrawBox( dl, mn, mx, p.Color, p.Opacity, p.CornerRadius, p.Sprite, sprites, p.SpriteBorder,
                                 scale );
                    }

                    if ( p.BorderWidth > 0.0f )
                        dl->AddRect( mn, mx, Col( p.BorderColor, 1.0f ), p.CornerRadius, 0,
                                     p.BorderWidth * scale );
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

                    if ( t.Shadow )
                        dl->AddText( ImGui::GetFont(), fs,
                                     ImVec2( tx + t.ShadowOffset.x * scale, ty + t.ShadowOffset.y * scale ),
                                     Col( t.ShadowColor, 1.0f ), t.Text.c_str() );
                    if ( t.Outline )
                    {
                        const ImU32 oc = Col( t.OutlineColor, 1.0f );
                        const float ow = std::max( 1.0f, scale );
                        for ( int ox = -1; ox <= 1; ++ox )
                            for ( int oy = -1; oy <= 1; ++oy )
                                if ( ox != 0 || oy != 0 )
                                    dl->AddText( ImGui::GetFont(), fs, ImVec2( tx + ox * ow, ty + oy * ow ), oc,
                                                 t.Text.c_str() );
                    }
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

        // If `e` is an auto-layout container, solve its children's rects exactly like the renderer's
        // DrawElement does — so hit-testing / handles match the drawn positions (children of a VBox/HBox/Grid
        // are placed by the group, NOT their own anchors). Fills kids + one rect each; empty when not a group.
        void SolveGroupChildren( entt::registry& reg, entt::entity e, const Rect& container, float scale,
                                 std::vector<entt::entity>& kids, std::vector<Rect>& rects )
        {
            if ( !reg.has<ECS::UILayoutGroupComponent>( e ) || !reg.has<ECS::RelationshipComponent>( e ) )
                return;
            const auto&            g = reg.get<ECS::UILayoutGroupComponent>( e ).Data;
            std::vector<glm::vec2> sizes;
            for ( auto c : reg.get<ECS::RelationshipComponent>( e ).Children )
            {
                if ( !reg.valid( c ) )
                    continue;
                glm::vec2 pref( 0.0f );
                if ( reg.has<ECS::UILayoutComponent>( c ) )
                {
                    const auto& L = reg.get<ECS::UILayoutComponent>( c ).Data;
                    pref          = glm::max( L.CustomMinimumSize, L.OffsetMax - L.OffsetMin );
                }
                kids.push_back( c );
                sizes.push_back( pref * scale );
            }
            LayoutGroupParams params;
            params.Type         = g.Type == ECS::UILayoutType::Horizontal ? LayoutGroupType::Horizontal
                                  : g.Type == ECS::UILayoutType::Grid     ? LayoutGroupType::Grid
                                                                          : LayoutGroupType::Vertical;
            params.PaddingL     = g.Padding.x * scale;
            params.PaddingT     = g.Padding.y * scale;
            params.PaddingR     = g.Padding.z * scale;
            params.PaddingB     = g.Padding.w * scale;
            params.Spacing      = g.Spacing * scale;
            params.StretchCross = g.StretchCross;
            params.CellSize     = g.CellSize * scale;
            params.Columns      = g.Columns;
            rects               = SolveLayoutGroup( container, params, sizes );
        }

        void PickRecurse( entt::registry& reg, entt::entity e, const Rect& parent, float scale, const glm::vec2& p,
                          entt::entity& hit, const Rect* forcedRect = nullptr )
        {
            Rect       rect      = parent;
            const bool hasLayout = reg.has<ECS::UILayoutComponent>( e );
            if ( forcedRect )
                rect = *forcedRect; // positioned by a parent auto-layout group
            else if ( hasLayout )
            {
                const auto& L = reg.get<ECS::UILayoutComponent>( e ).Data;
                rect          = ResolveRect( L.AnchorMin, L.AnchorMax, L.OffsetMin * scale, L.OffsetMax * scale,
                                             L.CustomMinimumSize * scale, parent );
            }
            if ( hasLayout ) // match the renderer's Aspect Ratio Fitter so hit-testing lines up
            {
                const auto& L = reg.get<ECS::UILayoutComponent>( e ).Data;
                rect          = ApplyAspectFit( rect, L.AspectRatio, static_cast<int>( L.AspectMode ) );
            }
            // Any element with a rect is selectable; later/deeper hits overwrite (matches draw order), so a
            // small button on top of a full-screen panel wins the pick instead of the panel behind it.
            if ( ( forcedRect || hasLayout ) && p.x >= rect.X && p.x <= rect.X + rect.W && p.y >= rect.Y &&
                 p.y <= rect.Y + rect.H )
                hit = e;

            if ( reg.has<ECS::RelationshipComponent>( e ) )
            {
                std::vector<entt::entity> kids;
                std::vector<Rect>         rects;
                SolveGroupChildren( reg, e, rect, scale, kids, rects );
                if ( !kids.empty() )
                    for ( std::size_t i = 0; i < kids.size(); ++i )
                        PickRecurse( reg, kids[i], rect, scale, p, hit, &rects[i] );
                else
                    for ( auto c : reg.get<ECS::RelationshipComponent>( e ).Children )
                        if ( reg.valid( c ) )
                            PickRecurse( reg, c, rect, scale, p, hit );
            }
        }

        void RectRecurse( entt::registry& reg, entt::entity e, const Rect& parent, float scale,
                          entt::entity target, Rect& out, bool& found, const Rect* forcedRect = nullptr )
        {
            Rect rect = parent;
            if ( forcedRect )
                rect = *forcedRect;
            else if ( reg.has<ECS::UILayoutComponent>( e ) )
            {
                const auto& L = reg.get<ECS::UILayoutComponent>( e ).Data;
                rect          = ResolveRect( L.AnchorMin, L.AnchorMax, L.OffsetMin * scale, L.OffsetMax * scale,
                                             L.CustomMinimumSize * scale, parent );
            }
            if ( reg.has<ECS::UILayoutComponent>( e ) )
            {
                const auto& L = reg.get<ECS::UILayoutComponent>( e ).Data;
                rect          = ApplyAspectFit( rect, L.AspectRatio, static_cast<int>( L.AspectMode ) );
            }
            if ( e == target )
            {
                out   = rect;
                found = true;
                return;
            }
            if ( reg.has<ECS::RelationshipComponent>( e ) )
            {
                std::vector<entt::entity> kids;
                std::vector<Rect>         rects;
                SolveGroupChildren( reg, e, rect, scale, kids, rects );
                if ( !kids.empty() )
                    for ( std::size_t i = 0; i < kids.size() && !found; ++i )
                        RectRecurse( reg, kids[i], rect, scale, target, out, found, &rects[i] );
                else
                    for ( auto c : reg.get<ECS::RelationshipComponent>( e ).Children )
                        if ( reg.valid( c ) && !found )
                            RectRecurse( reg, c, rect, scale, target, out, found );
            }
        }
    } // namespace

    bool RenderCanvas( entt::registry& reg, ImDrawList* dl, const Rect& viewportPx, bool interactive,
                       std::string* outClicked, const SpriteResolver& sprites, const glm::mat4* worldViewProj )
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
        Rect  canvasRect;
        float scale;
        if ( canvasData.RenderMode == ECS::UICanvasRenderMode::WorldSpace && worldViewProj &&
             reg.has<ECS::TransformComponent>( canvasEntity ) )
        {
            // Billboard: project the canvas entity's world position to the screen, centre + distance-scale it.
            const glm::vec3 wpos = glm::vec3( reg.get<ECS::TransformComponent>( canvasEntity ).GetTransform()[3] );
            const glm::vec4 clip = ( *worldViewProj ) * glm::vec4( wpos, 1.0f );
            if ( clip.w <= 0.0001f )
                return true; // behind the camera — a canvas exists, just nothing to draw
            const float sx = viewportPx.X + ( clip.x / clip.w * 0.5f + 0.5f ) * viewportPx.W;
            const float sy = viewportPx.Y + ( 1.0f - ( clip.y / clip.w * 0.5f + 0.5f ) ) * viewportPx.H;
            const float k  = canvasData.WorldScale / clip.w;
            const float w  = canvasData.ReferenceWidth * k;
            const float h  = canvasData.ReferenceHeight * k;
            canvasRect     = Rect{ sx - w * 0.5f, sy - h * 0.5f, w, h };
            scale          = k;
        }
        else
        {
            const CanvasFit fit = ResolveCanvas( canvasData, viewportPx );
            canvasRect          = fit.Root;
            scale               = fit.Scale;
        }

        // Optional full-canvas background image (drawn under the element tree).
        if ( sprites )
            if ( auto img = ResolveSpriteImage( canvasData.Sprite ) )
                if ( const void* tex = sprites( img ) )
                    dl->AddImage( (ImTextureID)tex, ImVec2( canvasRect.X, canvasRect.Y ),
                                  ImVec2( canvasRect.X + canvasRect.W, canvasRect.Y + canvasRect.H ) );

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
