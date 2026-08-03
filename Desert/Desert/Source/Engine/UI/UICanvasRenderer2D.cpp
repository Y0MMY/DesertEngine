#include "UICanvasRenderer2D.hpp"

#include <Engine/ECS/Components.hpp>
#include <Engine/Assets/Common.hpp>
#include <Engine/Graphic/Texture.hpp>
#include <Engine/Graphic/Image.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Text/FontBaker.hpp>

#include <algorithm>

namespace Desert::UI
{
    namespace
    {
        bool HandleSet( const Assets::AssetHandle& h )
        {
            return static_cast<uint64_t>( h ) != 0;
        }

        // Split a ';'-separated option string into its items (empty items skipped).
        std::vector<std::string> SplitOptions( const std::string& s )
        {
            std::vector<std::string> out;
            std::string              cur;
            for ( char c : s )
            {
                if ( c == ';' )
                {
                    if ( !cur.empty() )
                        out.push_back( cur );
                    cur.clear();
                }
                else
                    cur += c;
            }
            if ( !cur.empty() )
                out.push_back( cur );
            return out;
        }

        // An open dropdown whose option list is drawn AFTER the whole tree, so it overlays everything.
        struct PopupInfo
        {
            entt::entity Entity;
            Rect         Box;   // the dropdown's box rect (screen px)
            float        Scale; // canvas scale for its text
        };

        // Resolve a sprite AssetHandle to its runtime GPU Image2D (non-owning; the image service owns it and
        // Render2D keys its per-texture executor by the raw pointer). nullptr when unset / unresolvable.
        Graphic::Image2D* ResolveSpriteImage( const Assets::AssetHandle& handle )
        {
            if ( !HandleSet( handle ) )
                return nullptr;
            auto* texService = Runtime::ResourceRegistry::GetTextureService();
            if ( !texService )
                return nullptr;
            auto* tex = texService->Get( handle );
            if ( !tex )
                return nullptr;
            auto* imgService = Runtime::ResourceRegistry::GetImageService();
            if ( !imgService )
                return nullptr;
            return static_cast<Graphic::Image2D*>( imgService->Resolve( tex->GetImageHandle() ) );
        }

        // Draw a filled UI box: a sprite (tinted by `color`) when one is bound + resolvable, else a flat
        // colour. `srcBorder` (L,T,R,B in SOURCE pixels) enables 9-slice — corners stay unstretched (x
        // scale), edges/centre stretch — so image panels/buttons resize without distorting their borders.
        // Mirrors the ImGui DrawBox so both render paths look identical.
        void DrawBox( Graphic::Render2D::DrawList2D& dl, const glm::vec2& mn, const glm::vec2& mx,
                      const glm::vec4& color, const Assets::AssetHandle& sprite, const glm::vec4& srcBorder,
                      float scale, float rounding )
        {
            Graphic::Image2D* img = ResolveSpriteImage( sprite );
            if ( !img )
            {
                dl.AddRectFilled( mn, mx, color, rounding );
                return;
            }

            const void* tex = img;
            const float tw  = static_cast<float>( img->GetWidth() );
            const float th  = static_cast<float>( img->GetHeight() );
            const bool  nine =
                 tw > 0.0f && th > 0.0f &&
                 ( srcBorder.x > 0.0f || srcBorder.y > 0.0f || srcBorder.z > 0.0f || srcBorder.w > 0.0f );
            if ( !nine )
            {
                dl.AddImage( tex, mn, mx, { 0.0f, 0.0f }, { 1.0f, 1.0f }, color );
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
                    dl.AddImage( tex, { xs[c], ys[r] }, { xs[c + 1], ys[r + 1] }, { us[c], vs[r] },
                                 { us[c + 1], vs[r + 1] }, color );
        }

        // Lay a single line of text out into SDF glyph quads inside `rect`, aligned per the element. Uses the
        // FontService default font (TODO: a per-UIText font asset field, Roadmap Phase 5). `scale` maps design
        // px -> screen px; the SDF renders crisp at any resulting size.
        void DrawText2D( Graphic::Render2D::DrawList2D& dl, const ECS::UITextData& t, const Rect& rect,
                         float scale )
        {
            auto* fontService = Runtime::ResourceRegistry::GetFontService();
            if ( !fontService )
                return;
            Runtime::Font* font = fontService->Get( "Resources/Fonts/Roboto-Regular.ttf", 48.0f );
            if ( !font || !font->Atlas || !font->Baked.Valid() )
                return;

            const Text::BakedFont& bf    = font->Baked;
            const void*            atlas = font->Atlas.get();
            const float            s     = bf.PixelHeight > 0.0f ? ( t.FontSize * scale ) / bf.PixelHeight : 0.0f;
            if ( s <= 0.0f )
                return;

            auto glyph = [&]( char ch ) -> const Text::Glyph*
            {
                const auto it = bf.Glyphs.find( static_cast<uint32_t>( static_cast<unsigned char>( ch ) ) );
                return it == bf.Glyphs.end() ? nullptr : &it->second;
            };

            // Measure the line for horizontal alignment; vertical-centre in the element rect.
            float textW = 0.0f;
            for ( char ch : t.Text )
                if ( const Text::Glyph* g = glyph( ch ) )
                    textW += g->Advance * s;
            const float textH = ( bf.Ascent - bf.Descent ) * s;

            float startX = rect.X + 6.0f;
            if ( t.Align == ECS::UITextAlign::Center )
                startX = rect.X + ( rect.W - textW ) * 0.5f;
            else if ( t.Align == ECS::UITextAlign::Right )
                startX = rect.X + rect.W - textW - 6.0f;
            const float baselineY = rect.Y + ( rect.H - textH ) * 0.5f + bf.Ascent * s;

            // Emit the whole line, offset by `off` px and tinted `col`. Reused for shadow / outline / main so
            // all three stay in perfect glyph lock-step.
            auto emit = [&]( const glm::vec2& off, const glm::vec4& col )
            {
                float penX = startX;
                for ( char ch : t.Text )
                {
                    const Text::Glyph* g = glyph( ch );
                    if ( !g )
                        continue;
                    if ( g->Width > 0.0f && g->Height > 0.0f )
                    {
                        // OffsetY is the glyph top relative to the baseline, Y-down (negative above baseline).
                        const float x0 = penX + g->OffsetX * s + off.x;
                        const float y0 = baselineY + g->OffsetY * s + off.y;
                        dl.AddText( atlas, { x0, y0 }, { x0 + g->Width * s, y0 + g->Height * s }, { g->U0, g->V0 },
                                    { g->U1, g->V1 }, col );
                    }
                    penX += g->Advance * s;
                }
            };

            if ( t.Shadow )
                emit( t.ShadowOffset * scale, glm::vec4( t.ShadowColor, 1.0f ) );
            if ( t.Outline )
            {
                const glm::vec4 oc( t.OutlineColor, 1.0f );
                const float     ow = std::max( 1.0f, scale );
                for ( int ox = -1; ox <= 1; ++ox )
                    for ( int oy = -1; oy <= 1; ++oy )
                        if ( ox != 0 || oy != 0 )
                            emit( { ox * ow, oy * ow }, oc );
            }
            emit( { 0.0f, 0.0f }, glm::vec4( t.Color, 1.0f ) );
        }

        // Width in px of `text` at `fontSizePx` in the default font (for the input caret). 0 if no font.
        float MeasureTextPx( const std::string& text, float fontSizePx )
        {
            auto* fs = Runtime::ResourceRegistry::GetFontService();
            if ( !fs )
                return 0.0f;
            Runtime::Font* font = fs->Get( "Resources/Fonts/Roboto-Regular.ttf", 48.0f );
            if ( !font || !font->Baked.Valid() )
                return 0.0f;
            const Text::BakedFont& bf = font->Baked;
            const float            s  = bf.PixelHeight > 0.0f ? fontSizePx / bf.PixelHeight : 0.0f;
            float                  w  = 0.0f;
            for ( char ch : text )
            {
                const auto it = bf.Glyphs.find( static_cast<uint32_t>( static_cast<unsigned char>( ch ) ) );
                if ( it != bf.Glyphs.end() )
                    w += it->second.Advance * s;
            }
            return w;
        }

        // Remove the last UTF-8 codepoint (trailing continuation bytes + the lead/ASCII byte).
        void Utf8PopBack( std::string& s )
        {
            while ( !s.empty() && ( static_cast<unsigned char>( s.back() ) & 0xC0 ) == 0x80 )
                s.pop_back();
            if ( !s.empty() )
                s.pop_back();
        }

        // Maps the canvas to the viewport per its scale mode — mirrors ResolveCanvas in the ImGui renderer so
        // both paths agree on layout. Returns the canvas root rect (screen px) + the uniform scale applied to
        // every element's offsets / min-size.
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
                    return { viewportPx, 1.0f };
            }
        }

        // Recursively draw one element. `forcedRect` (non-null) is the rect assigned by a parent auto-layout
        // group — it overrides the element's own anchors for position + size.
        void DrawElement( entt::registry& reg, entt::entity e, const Rect& parent, float scale,
                          Graphic::Render2D::DrawList2D& dl, const UIInput* input, std::string* outClicked,
                          entt::entity* focused, std::vector<PopupInfo>* popups, const Rect* forcedRect = nullptr )
        {
            Rect       rect      = parent;
            const bool hasLayout = reg.has<ECS::UILayoutComponent>( e );
            if ( forcedRect )
                rect = *forcedRect; // positioned + sized by the parent's layout group
            else if ( hasLayout )
            {
                const auto& L = reg.get<ECS::UILayoutComponent>( e ).Data;
                rect          = ResolveRect( L.AnchorMin, L.AnchorMax, L.OffsetMin * scale, L.OffsetMax * scale,
                                             L.CustomMinimumSize * scale, parent );
            }

            if ( forcedRect || hasLayout )
            {
                const glm::vec2 mn( rect.X, rect.Y );
                const glm::vec2 mx( rect.X + rect.W, rect.Y + rect.H );

                // Panels and buttons render their sprite (single or 9-slice) tinted by the colour, or a flat
                // box when no sprite is bound. Button hover/press state needs input plumbing (a later slice),
                // so the normal state is drawn for now. Rounding / gradient / effects also come later.
                if ( reg.has<ECS::UIButtonComponent>( e ) )
                {
                    const auto& b     = reg.get<ECS::UIButtonComponent>( e ).Data;
                    const bool  hover = input && input->MousePx.x >= mn.x && input->MousePx.x <= mx.x &&
                                       input->MousePx.y >= mn.y && input->MousePx.y <= mx.y;
                    const bool      down = hover && input->MouseDown;
                    const glm::vec3 c    = down ? b.PressedColor : ( hover ? b.HoverColor : b.NormalColor );

                    // Image can change with state (hover / press), falling back to the normal Sprite.
                    Assets::AssetHandle spr = b.Sprite;
                    if ( down && HandleSet( b.PressedSprite ) )
                        spr = b.PressedSprite;
                    else if ( hover && HandleSet( b.HoverSprite ) )
                        spr = b.HoverSprite;
                    DrawBox( dl, mn, mx, glm::vec4( c, 1.0f ), spr, b.SpriteBorder, scale, 6.0f * scale );

                    if ( hover && outClicked && input->MouseReleased )
                    {
                        // Encode the structured action into the click message the runtime dispatches (same
                        // encoding as the ImGui renderer, so the host dispatcher is unchanged).
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
                        for ( int i = 0; i < layers; ++i ) // large faint -> small; overlap into a soft glow
                        {
                            const float ex = gs * ( 1.0f - static_cast<float>( i ) / layers );
                            dl.AddRectFilled( { mn.x - ex, mn.y - ex }, { mx.x + ex, mx.y + ex },
                                              glm::vec4( p.GlowColor, 0.10f * p.Opacity ), p.CornerRadius + ex );
                        }
                    }

                    if ( p.Shadow )
                        dl.AddRectFilled( { mn.x + p.ShadowOffset.x * scale, mn.y + p.ShadowOffset.y * scale },
                                          { mx.x + p.ShadowOffset.x * scale, mx.y + p.ShadowOffset.y * scale },
                                          glm::vec4( p.ShadowColor, p.Opacity ), p.CornerRadius * scale );

                    if ( p.UseGradient && !HandleSet( p.Sprite ) )
                        dl.AddRectFilledMultiColor( mn, mx, glm::vec4( p.Color, p.Opacity ),
                                                    glm::vec4( p.GradientColor, p.Opacity ) );
                    else
                        DrawBox( dl, mn, mx, glm::vec4( p.Color, p.Opacity ), p.Sprite, p.SpriteBorder, scale,
                                 p.CornerRadius * scale );

                    if ( p.BorderWidth > 0.0f )
                        dl.AddRect( mn, mx, glm::vec4( p.BorderColor, 1.0f ), p.BorderWidth * scale );
                }
                else if ( reg.has<ECS::UIProgressBarComponent>( e ) )
                {
                    const auto& pb = reg.get<ECS::UIProgressBarComponent>( e ).Data;
                    const float r  = pb.CornerRadius * scale;
                    dl.AddRectFilled( mn, mx, glm::vec4( pb.Background, 1.0f ), r );
                    const float t = std::clamp( pb.Value, 0.0f, 1.0f );
                    if ( t > 0.0f )
                        dl.AddRectFilled( mn, { mn.x + rect.W * t, mx.y }, glm::vec4( pb.Fill, 1.0f ), r );
                }
                else if ( reg.has<ECS::UIToggleComponent>( e ) )
                {
                    auto&      tg    = reg.get<ECS::UIToggleComponent>( e ).Data;
                    const bool hover = input && input->MousePx.x >= mn.x && input->MousePx.x <= mx.x &&
                                       input->MousePx.y >= mn.y && input->MousePx.y <= mx.y;
                    const float r = tg.CornerRadius * scale;
                    dl.AddRectFilled( mn, mx, glm::vec4( tg.BoxColor, 1.0f ), r );
                    if ( tg.Value )
                    {
                        const float pad = std::min( rect.W, rect.H ) * 0.22f; // inset "check" fill
                        dl.AddRectFilled( { mn.x + pad, mn.y + pad }, { mx.x - pad, mx.y - pad },
                                          glm::vec4( tg.CheckColor, 1.0f ), r * 0.5f );
                    }
                    if ( hover && input->MouseReleased )
                        tg.Value = !tg.Value;
                }
                else if ( reg.has<ECS::UISliderComponent>( e ) )
                {
                    auto&       sl    = reg.get<ECS::UISliderComponent>( e ).Data;
                    const float range = std::max( 0.0001f, sl.MaxValue - sl.MinValue );
                    const float t     = std::clamp( ( sl.Value - sl.MinValue ) / range, 0.0f, 1.0f );
                    const float pill  = rect.H * 0.5f; // fully-rounded track ends
                    const float fillX = mn.x + rect.W * t;
                    const float cy    = ( mn.y + mx.y ) * 0.5f;
                    const float hs    = rect.H * 0.6f; // handle half-size (circle via rounding)
                    dl.AddRectFilled( mn, mx, glm::vec4( sl.TrackColor, 1.0f ), pill );
                    if ( t > 0.0f )
                        dl.AddRectFilled( mn, { fillX, mx.y }, glm::vec4( sl.FillColor, 1.0f ), pill );
                    dl.AddRectFilled( { fillX - hs, cy - hs }, { fillX + hs, cy + hs },
                                      glm::vec4( sl.HandleColor, 1.0f ), hs );

                    const bool hover = input && input->MousePx.x >= mn.x && input->MousePx.x <= mx.x &&
                                       input->MousePx.y >= mn.y && input->MousePx.y <= mx.y;
                    if ( hover && input->MouseDown )
                    {
                        const float nt =
                             std::clamp( ( input->MousePx.x - mn.x ) / std::max( 1.0f, rect.W ), 0.0f, 1.0f );
                        sl.Value = sl.MinValue + nt * range;
                    }
                }
                else if ( reg.has<ECS::UIInputFieldComponent>( e ) )
                {
                    auto&      f         = reg.get<ECS::UIInputFieldComponent>( e ).Data;
                    const bool isFocused = focused && *focused == e;
                    const bool hover     = input && input->MousePx.x >= mn.x && input->MousePx.x <= mx.x &&
                                       input->MousePx.y >= mn.y && input->MousePx.y <= mx.y;

                    dl.AddRectFilled( mn, mx, glm::vec4( f.Background, 1.0f ), f.CornerRadius * scale );
                    if ( isFocused )
                        dl.AddRect( mn, mx, glm::vec4( f.FocusColor, 1.0f ), std::max( 1.0f, 2.0f * scale ) );

                    // Text (or dimmed placeholder), clipped to the field; caret at the end when focused.
                    const bool      showPlaceholder = f.Text.empty() && !isFocused;
                    ECS::UITextData td;
                    td.Text     = showPlaceholder ? f.Placeholder : f.Text;
                    td.FontSize = f.FontSize;
                    td.Color    = showPlaceholder ? f.PlaceholderColor : f.TextColor;
                    td.Align    = ECS::UITextAlign::Left;
                    dl.PushClipRect( mn, mx );
                    DrawText2D( dl, td, rect, scale );
                    if ( isFocused )
                    {
                        const float caretX = rect.X + 6.0f + MeasureTextPx( f.Text, f.FontSize * scale );
                        dl.AddRectFilled( { caretX, rect.Y + rect.H * 0.2f },
                                          { caretX + std::max( 1.0f, scale ), rect.Y + rect.H * 0.8f },
                                          glm::vec4( f.TextColor, 1.0f ) );
                    }
                    dl.PopClipRect();

                    if ( isFocused && input )
                    {
                        if ( !input->TypedText.empty() )
                            f.Text += input->TypedText;
                        if ( input->Backspace )
                            Utf8PopBack( f.Text );
                    }
                    if ( hover && input && input->MouseReleased && focused )
                        *focused = e; // click to focus
                }
                else if ( reg.has<ECS::UIDropdownComponent>( e ) )
                {
                    auto&      d       = reg.get<ECS::UIDropdownComponent>( e ).Data;
                    const auto options = SplitOptions( d.Options );

                    dl.AddRectFilled( mn, mx, glm::vec4( d.Background, 1.0f ), d.CornerRadius * scale );

                    ECS::UITextData td;
                    td.Text     = ( d.SelectedIndex >= 0 && d.SelectedIndex < (int)options.size() )
                                       ? options[d.SelectedIndex]
                                       : std::string();
                    td.FontSize = d.FontSize;
                    td.Color    = d.TextColor;
                    td.Align    = ECS::UITextAlign::Left;
                    DrawText2D( dl, td, rect, scale );

                    // Down-arrow on the right edge.
                    const float ax = mx.x - rect.H * 0.5f, ay = ( mn.y + mx.y ) * 0.5f, aw = rect.H * 0.16f;
                    dl.AddTriangleFilled( { ax - aw, ay - aw * 0.7f }, { ax + aw, ay - aw * 0.7f },
                                          { ax, ay + aw * 0.7f }, glm::vec4( d.TextColor, 1.0f ) );

                    const bool hover = input && input->MousePx.x >= mn.x && input->MousePx.x <= mx.x &&
                                       input->MousePx.y >= mn.y && input->MousePx.y <= mx.y;
                    if ( hover && input->MouseReleased )
                        d.Open = !d.Open;
                    if ( d.Open && popups )
                        popups->push_back( { e, rect, scale } ); // defer the option list to draw on top
                }

                if ( reg.has<ECS::UITextComponent2D>( e ) )
                    DrawText2D( dl, reg.get<ECS::UITextComponent2D>( e ).Data, rect, scale );
            }

            if ( reg.has<ECS::RelationshipComponent>( e ) )
            {
                Rect childParent = rect;
                // Clip Contents (RectMask2D) OR a scroll view both scissor children to this element's rect.
                bool clip = reg.has<ECS::UILayoutComponent>( e ) &&
                            reg.get<ECS::UILayoutComponent>( e ).Data.ClipContents;

                float scrollMaxPx = 0.0f; // >0 (scroll view overflowing) => draw a scrollbar afterward
                if ( reg.has<ECS::UIScrollViewComponent>( e ) )
                {
                    auto& sv = reg.get<ECS::UIScrollViewComponent>( e ).Data;
                    dl.AddRectFilled( { rect.X, rect.Y }, { rect.X + rect.W, rect.Y + rect.H },
                                      glm::vec4( sv.Background, 1.0f ) );

                    const float contentPx = sv.ContentHeight * scale;
                    scrollMaxPx           = std::max( 0.0f, contentPx - rect.H );
                    const bool hover      = input && input->MousePx.x >= rect.X &&
                                       input->MousePx.x <= rect.X + rect.W && input->MousePx.y >= rect.Y &&
                                       input->MousePx.y <= rect.Y + rect.H;
                    if ( hover && input->ScrollDelta != 0.0f )
                        sv.ScrollY -= input->ScrollDelta * 30.0f; // 30 design px per wheel notch
                    const float maxScrollDesign = scale > 0.0f ? scrollMaxPx / scale : 0.0f;
                    sv.ScrollY                  = std::clamp( sv.ScrollY, 0.0f, maxScrollDesign );

                    clip = true;
                    childParent.Y -= sv.ScrollY * scale; // shift children up by the scroll offset
                }

                if ( clip )
                    dl.PushClipRect( { rect.X, rect.Y }, { rect.X + rect.W, rect.Y + rect.H } );

                const auto& children = reg.get<ECS::RelationshipComponent>( e ).Children;
                if ( reg.has<ECS::UILayoutGroupComponent>( e ) )
                {
                    // Auto-layout: the group positions + sizes its children (overriding their anchors). Each
                    // child's preferred size = CustomMinimumSize, else its authored offset size (design px).
                    const auto&               g = reg.get<ECS::UILayoutGroupComponent>( e ).Data;
                    std::vector<entt::entity> kids;
                    std::vector<glm::vec2>    sizes;
                    for ( auto c : children )
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

                    const auto rects = SolveLayoutGroup( childParent, params, sizes );
                    for ( std::size_t i = 0; i < kids.size(); ++i )
                        DrawElement( reg, kids[i], childParent, scale, dl, input, outClicked, focused, popups,
                                     &rects[i] );
                }
                else
                {
                    for ( auto c : children )
                        if ( reg.valid( c ) )
                            DrawElement( reg, c, childParent, scale, dl, input, outClicked, focused, popups );
                }
                if ( clip )
                    dl.PopClipRect();

                // Scroll thumb on the right edge (outside the clip), shown only when the content overflows.
                if ( reg.has<ECS::UIScrollViewComponent>( e ) )
                {
                    const auto& sv = reg.get<ECS::UIScrollViewComponent>( e ).Data;
                    if ( sv.ShowScrollbar && scrollMaxPx > 0.0f )
                    {
                        const float barW      = 6.0f * scale;
                        const float trackX    = rect.X + rect.W - barW;
                        const float contentPx = sv.ContentHeight * scale;
                        const float thumbH    = std::max( barW * 2.0f, rect.H * ( rect.H / contentPx ) );
                        const float t         = ( sv.ScrollY * scale ) / scrollMaxPx;
                        const float thumbY    = rect.Y + t * ( rect.H - thumbH );
                        dl.AddRectFilled( { trackX, thumbY }, { rect.X + rect.W, thumbY + thumbH },
                                          glm::vec4( sv.ScrollbarColor, 1.0f ), barW * 0.5f );
                    }
                }
            }
        }
    } // namespace

    bool RenderCanvas2D( entt::registry& reg, Graphic::Render2D::DrawList2D& dl, const Rect& viewportPx,
                         const glm::mat4* worldViewProj, const UIInput* input, std::string* outClicked,
                         entt::entity* focused )
    {
        auto canvasView = reg.view<ECS::UICanvasComponent>();
        if ( canvasView.begin() == canvasView.end() )
            return false;

        const entt::entity canvasEntity = *canvasView.begin();
        const auto&        canvasData   = reg.get<ECS::UICanvasComponent>( canvasEntity ).Data;
        if ( !canvasData.Visible )
            return false;

        Rect  canvasRect;
        float scale;
        if ( canvasData.RenderMode == ECS::UICanvasRenderMode::WorldSpace && worldViewProj &&
             reg.has<ECS::TransformComponent>( canvasEntity ) )
        {
            // Billboard: project the canvas entity's world position to the screen, centre + distance-scale it
            // (mirrors the ImGui renderer so world-space UI matches).
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

        std::vector<PopupInfo> popups;
        if ( reg.has<ECS::RelationshipComponent>( canvasEntity ) )
            for ( auto c : reg.get<ECS::RelationshipComponent>( canvasEntity ).Children )
                if ( reg.valid( c ) )
                    DrawElement( reg, c, canvasRect, scale, dl, input, outClicked, focused, &popups );

        // Open dropdown option lists, drawn LAST so they overlay everything.
        for ( const PopupInfo& pi : popups )
        {
            if ( !reg.valid( pi.Entity ) || !reg.has<ECS::UIDropdownComponent>( pi.Entity ) )
                continue;
            auto&       d       = reg.get<ECS::UIDropdownComponent>( pi.Entity ).Data;
            const auto  options = SplitOptions( d.Options );
            const float rowH    = pi.Box.H;
            const Rect  popup{ pi.Box.X, pi.Box.Y + pi.Box.H, pi.Box.W,
                              rowH * static_cast<float>( options.size() ) };
            dl.AddRectFilled( { popup.X, popup.Y }, { popup.X + popup.W, popup.Y + popup.H },
                              glm::vec4( d.Background, 1.0f ), d.CornerRadius * pi.Scale );

            bool clickedOption = false;
            for ( std::size_t i = 0; i < options.size(); ++i )
            {
                const Rect row{ popup.X, popup.Y + static_cast<float>( i ) * rowH, popup.W, rowH };
                const bool hover = input && input->MousePx.x >= row.X && input->MousePx.x <= row.X + row.W &&
                                   input->MousePx.y >= row.Y && input->MousePx.y <= row.Y + row.H;
                if ( hover )
                    dl.AddRectFilled( { row.X, row.Y }, { row.X + row.W, row.Y + row.H },
                                      glm::vec4( d.Highlight, 1.0f ) );
                ECS::UITextData td;
                td.Text     = options[i];
                td.FontSize = d.FontSize;
                td.Color    = d.TextColor;
                td.Align    = ECS::UITextAlign::Left;
                DrawText2D( dl, td, row, pi.Scale );
                if ( hover && input->MouseReleased )
                {
                    d.SelectedIndex = static_cast<int>( i );
                    d.Open          = false;
                    clickedOption   = true;
                }
            }
            // A click outside both the popup and the box closes it (the box click is toggled in the walk).
            if ( input && input->MouseReleased && !clickedOption )
            {
                const bool inPopup = input->MousePx.x >= popup.X && input->MousePx.x <= popup.X + popup.W &&
                                     input->MousePx.y >= popup.Y && input->MousePx.y <= popup.Y + popup.H;
                const bool inBox = input->MousePx.x >= pi.Box.X && input->MousePx.x <= pi.Box.X + pi.Box.W &&
                                   input->MousePx.y >= pi.Box.Y && input->MousePx.y <= pi.Box.Y + pi.Box.H;
                if ( !inPopup && !inBox )
                    d.Open = false;
            }
        }

        return true;
    }
} // namespace Desert::UI
