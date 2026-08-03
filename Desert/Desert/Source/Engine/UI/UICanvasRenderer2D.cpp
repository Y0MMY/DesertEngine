#include "UICanvasRenderer2D.hpp"

#include <Engine/ECS/Components.hpp>
#include <Engine/Assets/Common.hpp>
#include <Engine/Graphic/Texture.hpp>
#include <Engine/Graphic/Image.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Text/FontBaker.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <vector>

namespace Desert::UI
{
    namespace
    {
        bool HandleSet( const Assets::AssetHandle& h )
        {
            return static_cast<uint64_t>( h ) != 0;
        }

        // Seconds since the first UI frame — a shared wall clock so every time-driven effect (pulse, marquee,
        // hover eases) animates without any per-frame dt being plumbed through the stateless walk.
        float NowSeconds()
        {
            static const auto epoch = std::chrono::steady_clock::now();
            return std::chrono::duration<float>( std::chrono::steady_clock::now() - epoch ).count();
        }

        // Per-button hover interpolation (0=rest, 1=hovered), eased each frame toward the target so hover
        // colours cross-fade instead of snapping. Keyed by entity (transient, non-serialized). s_FrameDt is
        // the wall-clock delta of the current frame, refreshed once at the top of RenderCanvas2D.
        std::unordered_map<entt::entity, float> s_HoverT;
        float                                   s_FrameDt = 0.0f;

        float HoverEase( entt::entity e, bool hovered )
        {
            float&      t = s_HoverT[e];
            const float k = std::clamp( s_FrameDt * 12.0f, 0.0f, 1.0f ); // exponential approach
            t += ( ( hovered ? 1.0f : 0.0f ) - t ) * k;
            return t;
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

        // Keyboard-focusable controls (Tab cycles between them; Enter activates the focused one).
        bool IsFocusable( entt::registry& reg, entt::entity e )
        {
            return reg.has<ECS::UIButtonComponent>( e ) || reg.has<ECS::UIInputFieldComponent>( e ) ||
                   reg.has<ECS::UIToggleComponent>( e ) || reg.has<ECS::UISliderComponent>( e ) ||
                   reg.has<ECS::UIDropdownComponent>( e );
        }

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
            // An animated (GIF) sprite plays its current frame (a pure function of wall-clock time), drawn
            // stretched to the box; static sprites / 9-slice keep the path below. Non-GIF handles resolve to
            // nullptr here, so ordinary textures are unaffected.
            if ( auto* animService = Runtime::ResourceRegistry::GetAnimatedImageService() )
                if ( Graphic::Image2D* frame = animService->Resolve( sprite ) )
                {
                    dl.AddImage( frame, mn, mx, { 0.0f, 0.0f }, { 1.0f, 1.0f }, color );
                    return;
                }

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

        // --- Text layout (Phase E: rich text + word-wrap + auto-size + vertical align + overflow) --------
        // Lays a UIText string into SDF glyph quads inside `rect`. `scale` maps design px -> screen px; the SDF
        // stays crisp at any resulting size. Handles the per-element font asset, multi-line + word-wrap,
        // line-spacing, horizontal/vertical alignment, auto-size, overflow (ellipsis/clip) and rich text.

        // One character carrying its resolved rich-text style. `bold` => faux-bold (the glyph is drawn a
        // second time nudged in X, since the atlas has a single weight).
        struct StyledChar
        {
            char      ch    = 0;
            glm::vec4 color = glm::vec4( 1.0f );
            bool      bold  = false;
        };

        // A laid-out line: its characters plus the total advance width in EM units (baked-pixel space, before
        // the on-screen scale `s` is applied). Kept in em so auto-size can rescale without re-measuring glyphs.
        struct TextLine
        {
            std::vector<StyledChar> chars;
            float                   width = 0.0f;
        };

        // A positioned glyph quad in pixel space — computed once, then drawn for shadow / outline / main so
        // every pass stays in perfect lock-step.
        struct PlacedGlyph
        {
            float     x0, y0, x1, y1;
            float     u0, v0, u1, v1;
            glm::vec4 color;
            bool      bold;
        };

        // "rrggbb" or "rrggbbaa" -> vec4. false (and out untouched) on any malformed input.
        bool ParseHexColor( const std::string& hex, glm::vec4& out )
        {
            auto nib = []( char c ) -> int
            {
                if ( c >= '0' && c <= '9' )
                    return c - '0';
                if ( c >= 'a' && c <= 'f' )
                    return c - 'a' + 10;
                if ( c >= 'A' && c <= 'F' )
                    return c - 'A' + 10;
                return -1;
            };
            if ( hex.size() != 6 && hex.size() != 8 )
                return false;
            int v[8];
            for ( size_t k = 0; k < hex.size(); ++k )
                if ( ( v[k] = nib( hex[k] ) ) < 0 )
                    return false;
            out.r = ( v[0] * 16 + v[1] ) / 255.0f;
            out.g = ( v[2] * 16 + v[3] ) / 255.0f;
            out.b = ( v[4] * 16 + v[5] ) / 255.0f;
            out.a = hex.size() == 8 ? ( v[6] * 16 + v[7] ) / 255.0f : 1.0f;
            return true;
        }

        // Expand text into styled characters. When `rich`, parse a BBCode subset — [color=#rrggbb]..[/color]
        // (nestable) and [b]..[/b] — consuming the tags; an unrecognised '[' is emitted literally.
        std::vector<StyledChar> BuildStyledChars( const std::string& text, const glm::vec4& baseColor, bool rich )
        {
            std::vector<StyledChar> out;
            out.reserve( text.size() );
            if ( !rich )
            {
                for ( char ch : text )
                    out.push_back( { ch, baseColor, false } );
                return out;
            }

            std::vector<glm::vec4> colorStack{ baseColor };
            int                    boldDepth = 0;
            for ( size_t i = 0; i < text.size(); )
            {
                if ( text[i] == '[' )
                {
                    const size_t close = text.find( ']', i );
                    if ( close != std::string::npos )
                    {
                        const std::string tag     = text.substr( i + 1, close - i - 1 );
                        bool              handled = true;
                        if ( tag == "b" )
                            ++boldDepth;
                        else if ( tag == "/b" )
                            boldDepth = std::max( 0, boldDepth - 1 );
                        else if ( tag == "/color" )
                        {
                            if ( colorStack.size() > 1 )
                                colorStack.pop_back();
                        }
                        else if ( tag.rfind( "color=#", 0 ) == 0 )
                        {
                            glm::vec4 c = colorStack.back();
                            ParseHexColor( tag.substr( 7 ), c ); // keep parent colour on malformed hex
                            colorStack.push_back( c );
                        }
                        else
                            handled = false;

                        if ( handled )
                        {
                            i = close + 1;
                            continue;
                        }
                    }
                }
                out.push_back( { text[i], colorStack.back(), boldDepth > 0 } );
                ++i;
            }
            return out;
        }

        // Greedy word-wrap into lines. `adv(ch)` gives a glyph's em advance; explicit '\n' always breaks.
        // A word longer than maxWidthEm overflows its own line rather than being character-split.
        template <typename AdvFn>
        std::vector<TextLine> LayoutLines( const std::vector<StyledChar>& chars, AdvFn adv, float maxWidthEm,
                                           bool wrap )
        {
            std::vector<TextLine>   lines;
            TextLine                cur;
            std::vector<StyledChar> word;
            float                   wordW = 0.0f;

            auto flushWord = [&]()
            {
                for ( const auto& c : word )
                {
                    cur.chars.push_back( c );
                    cur.width += adv( c.ch );
                }
                word.clear();
                wordW = 0.0f;
            };
            auto trimTrailingSpaces = [&]()
            {
                while ( !cur.chars.empty() && cur.chars.back().ch == ' ' )
                {
                    cur.width -= adv( ' ' );
                    cur.chars.pop_back();
                }
            };
            auto pushLine = [&]()
            {
                lines.push_back( std::move( cur ) );
                cur = TextLine{};
            };

            for ( const StyledChar& c : chars )
            {
                if ( c.ch == '\n' )
                {
                    flushWord();
                    pushLine();
                    continue;
                }
                if ( c.ch == ' ' )
                {
                    flushWord();
                    cur.chars.push_back( c );
                    cur.width += adv( ' ' );
                    continue;
                }
                word.push_back( c );
                wordW += adv( c.ch );
                if ( wrap && !cur.chars.empty() && ( cur.width + wordW ) > maxWidthEm )
                {
                    trimTrailingSpaces();
                    pushLine(); // the pending word carries over and starts the fresh line
                }
            }
            flushWord();
            trimTrailingSpaces();
            lines.push_back( std::move( cur ) );
            return lines;
        }

        void DrawText2D( Graphic::Render2D::DrawList2D& dl, const ECS::UITextData& t, const Rect& rect,
                         float scale )
        {
            if ( t.Text.empty() )
                return;

            auto* fontService = Runtime::ResourceRegistry::GetFontService();
            if ( !fontService )
                return;
            // Font is an asset handle on the element (drag-drop / preloaded); unset falls back to the default.
            const uint64_t fontHandle = static_cast<uint64_t>( t.Font ) != 0 ? static_cast<uint64_t>( t.Font )
                                                                             : fontService->DefaultFontHandle();
            Runtime::Font* font       = fontService->Get( fontHandle, 48.0f );
            if ( !font || !font->Atlas || !font->Baked.Valid() || font->Baked.PixelHeight <= 0.0f )
                return;

            const Text::BakedFont& bf    = font->Baked;
            const void*            atlas = font->Atlas.get();

            auto glyph = [&]( char ch ) -> const Text::Glyph*
            {
                const auto it = bf.Glyphs.find( static_cast<uint32_t>( static_cast<unsigned char>( ch ) ) );
                return it == bf.Glyphs.end() ? nullptr : &it->second;
            };
            auto advEm = [&]( char ch ) -> float
            {
                const Text::Glyph* g = glyph( ch );
                return g ? g->Advance : 0.0f;
            };

            const std::vector<StyledChar> chars =
                 BuildStyledChars( t.Text, glm::vec4( t.Color, 1.0f ), t.RichText );

            // Marquee: a single clipped line scrolling leftward, repeated seamlessly across the width. A news
            // ticker / running banner. Pure function of the shared clock, so it needs no per-frame state.
            if ( t.Marquee )
            {
                const float sM       = ( t.FontSize * scale ) / bf.PixelHeight;
                float       contentW = 0.0f;
                for ( const StyledChar& sc : chars )
                    contentW += advEm( sc.ch ) * sM;
                const float gap    = std::max( 40.0f * scale, rect.W * 0.35f );
                const float period = std::max( 1.0f, contentW + gap );
                const float off    = std::fmod( NowSeconds() * t.MarqueeSpeed * scale, period );
                const float blockH = ( bf.Ascent - bf.Descent ) * sM;
                const float baseY  = rect.Y + ( rect.H - blockH ) * 0.5f + bf.Ascent * sM;

                dl.PushClipRect( { rect.X, rect.Y }, { rect.X + rect.W, rect.Y + rect.H } );
                for ( float startX = rect.X - off; startX < rect.X + rect.W; startX += period )
                {
                    float penX = startX;
                    for ( const StyledChar& sc : chars )
                    {
                        const Text::Glyph* g = glyph( sc.ch );
                        if ( !g )
                            continue;
                        if ( g->Width > 0.0f && g->Height > 0.0f )
                        {
                            const float x0 = penX + g->OffsetX * sM;
                            const float y0 = baseY + g->OffsetY * sM;
                            dl.AddText( atlas, { x0, y0 }, { x0 + g->Width * sM, y0 + g->Height * sM },
                                        { g->U0, g->V0 }, { g->U1, g->V1 }, sc.color );
                        }
                        penX += g->Advance * sM;
                    }
                }
                dl.PopClipRect();
                return;
            }

            const float pad    = 6.0f;
            const float availW = std::max( 1.0f, rect.W - 2.0f * pad );
            const float availH = std::max( 1.0f, rect.H - 2.0f * pad );
            // Em-space vertical advance per line: the font's line box times the user's line-spacing multiplier.
            const float lineHemEm = ( bf.Ascent - bf.Descent ) * std::max( 0.1f, t.LineSpacing );

            auto layoutAt = [&]( float s ) -> std::vector<TextLine>
            {
                const float threshold = t.Wrap ? availW / s : std::numeric_limits<float>::max();
                return LayoutLines( chars, advEm, threshold, t.Wrap );
            };

            float                 s     = ( t.FontSize * scale ) / bf.PixelHeight;
            std::vector<TextLine> lines = layoutAt( s );

            if ( t.AutoSize )
            {
                // Shrink toward the floor until the block fits height (and width when not wrapping).
                const float minS = std::max( 0.01f, ( t.MinFontSize * scale ) / bf.PixelHeight );
                for ( int iter = 0; iter < 24; ++iter )
                {
                    float maxLineEm = 0.0f;
                    for ( const auto& ln : lines )
                        maxLineEm = std::max( maxLineEm, ln.width );
                    const float blockH = lines.empty() ? 0.0f
                                                       : ( static_cast<float>( lines.size() - 1 ) * lineHemEm * s +
                                                           ( bf.Ascent - bf.Descent ) * s );
                    const bool  fitsH  = blockH <= availH;
                    const bool  fitsW  = t.Wrap || ( maxLineEm * s <= availW );
                    if ( ( fitsH && fitsW ) || s <= minS )
                        break;
                    s     = std::max( minS, s * 0.92f );
                    lines = layoutAt( s );
                }
            }

            const float lineStep = lineHemEm * s;
            if ( lineStep <= 0.0f )
                return;

            // Overflow: drop the lines that fall past the bottom (Ellipsis/Clip), tagging the tail as truncated.
            bool truncated = false;
            if ( t.Overflow != ECS::UITextOverflow::Overflow )
            {
                const size_t maxVisible =
                     std::max<size_t>( 1, static_cast<size_t>( std::floor( availH / lineStep ) ) );
                if ( lines.size() > maxVisible )
                {
                    lines.resize( maxVisible );
                    truncated = ( t.Overflow == ECS::UITextOverflow::Ellipsis );
                }
            }

            // Ellipsis: trim trailing glyphs off any over-wide line (and the truncated tail) and append "...".
            if ( t.Overflow == ECS::UITextOverflow::Ellipsis )
            {
                const float dotAdv    = advEm( '.' );
                auto        ellipsize = [&]( TextLine& ln )
                {
                    const float dots = 3.0f * dotAdv;
                    while ( !ln.chars.empty() && ( ln.width + dots ) * s > availW )
                    {
                        ln.width -= advEm( ln.chars.back().ch );
                        ln.chars.pop_back();
                    }
                    const glm::vec4 col = ln.chars.empty() ? glm::vec4( t.Color, 1.0f ) : ln.chars.back().color;
                    for ( int k = 0; k < 3; ++k )
                    {
                        ln.chars.push_back( { '.', col, false } );
                        ln.width += dotAdv;
                    }
                };
                if ( dotAdv > 0.0f )
                {
                    if ( !t.Wrap )
                        for ( auto& ln : lines )
                            if ( ln.width * s > availW )
                                ellipsize( ln );
                    if ( truncated && !lines.empty() )
                        ellipsize( lines.back() );
                }
            }

            // Vertical placement of the whole block within the rect.
            const float blockH =
                 lines.empty()
                      ? 0.0f
                      : ( static_cast<float>( lines.size() - 1 ) * lineStep + ( bf.Ascent - bf.Descent ) * s );
            float blockTop = rect.Y + pad; // Top
            if ( t.VerticalAlign == ECS::UITextVAlign::Middle )
                blockTop = rect.Y + ( rect.H - blockH ) * 0.5f;
            else if ( t.VerticalAlign == ECS::UITextVAlign::Bottom )
                blockTop = rect.Y + rect.H - pad - blockH;

            // Position every glyph (per-line horizontal alignment), collecting quads for the draw passes.
            std::vector<PlacedGlyph> placed;
            for ( size_t i = 0; i < lines.size(); ++i )
            {
                const float lineWpx = lines[i].width * s;
                float       penX    = rect.X + pad; // Left
                if ( t.Align == ECS::UITextAlign::Center )
                    penX = rect.X + ( rect.W - lineWpx ) * 0.5f;
                else if ( t.Align == ECS::UITextAlign::Right )
                    penX = rect.X + rect.W - pad - lineWpx;
                const float baselineY = blockTop + static_cast<float>( i ) * lineStep + bf.Ascent * s;

                for ( const StyledChar& sc : lines[i].chars )
                {
                    const Text::Glyph* g = glyph( sc.ch );
                    if ( !g )
                        continue;
                    if ( g->Width > 0.0f && g->Height > 0.0f )
                    {
                        // OffsetY is the glyph top relative to the baseline, Y-down (negative above baseline).
                        const float x0 = penX + g->OffsetX * s;
                        const float y0 = baselineY + g->OffsetY * s;
                        placed.push_back( { x0, y0, x0 + g->Width * s, y0 + g->Height * s, g->U0, g->V0, g->U1,
                                            g->V1, glm::vec4( glm::vec3( sc.color ), sc.color.a ), sc.bold } );
                    }
                    penX += g->Advance * s;
                }
            }

            const bool clip = ( t.Overflow == ECS::UITextOverflow::Clip );
            if ( clip )
                dl.PushClipRect( { rect.X, rect.Y }, { rect.X + rect.W, rect.Y + rect.H } );

            auto quad = [&]( const PlacedGlyph& g, const glm::vec2& off, const glm::vec4& col )
            {
                dl.AddText( atlas, { g.x0 + off.x, g.y0 + off.y }, { g.x1 + off.x, g.y1 + off.y }, { g.u0, g.v0 },
                            { g.u1, g.v1 }, col );
            };

            if ( t.Shadow )
            {
                const glm::vec4 sc = glm::vec4( t.ShadowColor, 1.0f );
                const glm::vec2 so = t.ShadowOffset * scale;
                for ( const auto& g : placed )
                    quad( g, so, sc );
            }
            if ( t.Outline )
            {
                const glm::vec4 oc( t.OutlineColor, 1.0f );
                const float     ow = std::max( 1.0f, scale );
                for ( int ox = -1; ox <= 1; ++ox )
                    for ( int oy = -1; oy <= 1; ++oy )
                        if ( ox != 0 || oy != 0 )
                            for ( const auto& g : placed )
                                quad( g, { ox * ow, oy * ow }, oc );
            }
            for ( const auto& g : placed )
            {
                quad( g, { 0.0f, 0.0f }, g.color );
                if ( g.bold ) // faux-bold: a second pass nudged in X thickens the stroke
                    quad( g, { std::max( 0.5f, 0.6f * scale ), 0.0f }, g.color );
            }

            if ( clip )
                dl.PopClipRect();
        }

        // Width in px of `text` at `fontSizePx` in the default font (for the input caret). 0 if no font.
        float MeasureTextPx( const std::string& text, float fontSizePx )
        {
            auto* fs = Runtime::ResourceRegistry::GetFontService();
            if ( !fs )
                return 0.0f;
            Runtime::Font* font = fs->Get( fs->DefaultFontHandle(), 48.0f );
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

        // Draw a built-in vector icon centred in `rect`, sized to `sizeFrac` of the shorter side. Composed
        // from Render2D primitives (lines / triangles / filled circles / rings) so it stays crisp at any
        // scale with no icon-font or SVG dependency. `th` is the stroke width in px (already scaled).
        void DrawIcon( Graphic::Render2D::DrawList2D& dl, ECS::UIIconType type, const Rect& rect,
                       const glm::vec4& col, float th, float sizeFrac )
        {
            const glm::vec2 c( rect.X + rect.W * 0.5f, rect.Y + rect.H * 0.5f );
            const float     R = std::min( rect.W, rect.H ) * 0.5f * std::clamp( sizeFrac, 0.1f, 1.0f );
            if ( R <= 0.0f )
                return;

            auto            line = [&]( glm::vec2 a, glm::vec2 b ) { dl.AddLine( a, b, col, th ); };
            auto            dot  = [&]( glm::vec2 p, float r ) { dl.AddRectFilled( p - r, p + r, col, r ); };
            auto            ring = [&]( float outerR ) { dl.AddRing( c, outerR, outerR - th, col, col ); };
            auto            rectOutline = [&]( glm::vec2 mn, glm::vec2 mx ) { dl.AddRect( mn, mx, col, th ); };
            constexpr float PI          = 3.14159265358979f;

            switch ( type )
            {
                case ECS::UIIconType::Play:
                    dl.AddTriangleFilled( { c.x - R * 0.5f, c.y - R * 0.75f }, { c.x - R * 0.5f, c.y + R * 0.75f },
                                          { c.x + R * 0.8f, c.y }, col );
                    break;
                case ECS::UIIconType::User:
                    dot( { c.x, c.y - R * 0.38f }, R * 0.34f ); // head
                    dl.AddRectFilled( { c.x - R * 0.6f, c.y + R * 0.12f }, { c.x + R * 0.6f, c.y + R * 0.95f },
                                      col, R * 0.45f ); // shoulders
                    break;
                case ECS::UIIconType::Server:
                    rectOutline( { c.x - R * 0.85f, c.y - R * 0.6f }, { c.x + R * 0.85f, c.y - R * 0.05f } );
                    rectOutline( { c.x - R * 0.85f, c.y + R * 0.05f }, { c.x + R * 0.85f, c.y + R * 0.6f } );
                    dot( { c.x - R * 0.55f, c.y - R * 0.32f }, th * 0.7f );
                    dot( { c.x - R * 0.55f, c.y + R * 0.32f }, th * 0.7f );
                    break;
                case ECS::UIIconType::Cart:
                    line( { c.x - R * 0.85f, c.y - R * 0.6f }, { c.x - R * 0.5f, c.y - R * 0.35f } );  // handle
                    line( { c.x - R * 0.5f, c.y - R * 0.35f }, { c.x + R * 0.7f, c.y - R * 0.35f } );  // top
                    line( { c.x + R * 0.7f, c.y - R * 0.35f }, { c.x + R * 0.4f, c.y + R * 0.35f } );  // right
                    line( { c.x - R * 0.5f, c.y - R * 0.35f }, { c.x - R * 0.25f, c.y + R * 0.35f } ); // left
                    line( { c.x - R * 0.25f, c.y + R * 0.35f }, { c.x + R * 0.4f, c.y + R * 0.35f } ); // bottom
                    dot( { c.x - R * 0.15f, c.y + R * 0.65f }, th * 0.9f );
                    dot( { c.x + R * 0.35f, c.y + R * 0.65f }, th * 0.9f );
                    break;
                case ECS::UIIconType::Gear:
                    ring( R * 0.55f );
                    dot( c, R * 0.18f );
                    for ( int k = 0; k < 8; ++k )
                    {
                        const float     a = k * PI / 4.0f;
                        const glm::vec2 d( std::cos( a ), std::sin( a ) );
                        line( c + d * ( R * 0.5f ), c + d * ( R * 0.88f ) );
                    }
                    break;
                case ECS::UIIconType::Power:
                    ring( R * 0.6f );
                    line( { c.x, c.y - R * 0.9f }, { c.x, c.y + R * 0.05f } ); // top stroke through the ring
                    break;
                case ECS::UIIconType::Star:
                {
                    glm::vec2 pts[10];
                    for ( int i = 0; i < 10; ++i )
                    {
                        const float a  = -PI * 0.5f + i * PI / 5.0f;
                        const float rr = ( i % 2 == 0 ) ? R : R * 0.42f;
                        pts[i]         = c + glm::vec2( std::cos( a ), std::sin( a ) ) * rr;
                    }
                    for ( int i = 0; i < 10; ++i )
                        dl.AddTriangleFilled( c, pts[i], pts[( i + 1 ) % 10], col );
                    break;
                }
                case ECS::UIIconType::Heart:
                    dot( { c.x - R * 0.35f, c.y - R * 0.18f }, R * 0.4f );
                    dot( { c.x + R * 0.35f, c.y - R * 0.18f }, R * 0.4f );
                    dl.AddTriangleFilled( { c.x - R * 0.72f, c.y - R * 0.05f },
                                          { c.x + R * 0.72f, c.y - R * 0.05f }, { c.x, c.y + R * 0.85f }, col );
                    break;
                case ECS::UIIconType::Check:
                    line( { c.x - R * 0.6f, c.y + R * 0.05f }, { c.x - R * 0.12f, c.y + R * 0.5f } );
                    line( { c.x - R * 0.12f, c.y + R * 0.5f }, { c.x + R * 0.62f, c.y - R * 0.45f } );
                    break;
                case ECS::UIIconType::Close:
                    line( { c.x - R * 0.55f, c.y - R * 0.55f }, { c.x + R * 0.55f, c.y + R * 0.55f } );
                    line( { c.x - R * 0.55f, c.y + R * 0.55f }, { c.x + R * 0.55f, c.y - R * 0.55f } );
                    break;
                case ECS::UIIconType::ChevronRight:
                    line( { c.x - R * 0.25f, c.y - R * 0.55f }, { c.x + R * 0.35f, c.y } );
                    line( { c.x + R * 0.35f, c.y }, { c.x - R * 0.25f, c.y + R * 0.55f } );
                    break;
                case ECS::UIIconType::Bell:
                    dl.AddRectFilled( { c.x - R * 0.42f, c.y - R * 0.55f }, { c.x + R * 0.42f, c.y + R * 0.28f },
                                      col, R * 0.4f );                                                  // body
                    line( { c.x - R * 0.62f, c.y + R * 0.32f }, { c.x + R * 0.62f, c.y + R * 0.32f } ); // rim
                    dot( { c.x, c.y + R * 0.58f }, th * 0.85f );                                        // clapper
                    break;
            }
        }

        // Content size (px) a layout-group container needs to hug its children — for the Content Size Fitter.
        glm::vec2 GroupContentPx( entt::registry& reg, entt::entity e, float scale )
        {
            if ( !reg.has<ECS::UILayoutGroupComponent>( e ) || !reg.has<ECS::RelationshipComponent>( e ) )
                return { 0.0f, 0.0f };
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
                sizes.push_back( pref * scale );
            }
            LayoutGroupParams params;
            params.Type     = g.Type == ECS::UILayoutType::Horizontal ? LayoutGroupType::Horizontal
                              : g.Type == ECS::UILayoutType::Grid     ? LayoutGroupType::Grid
                                                                      : LayoutGroupType::Vertical;
            params.PaddingL = g.Padding.x * scale;
            params.PaddingT = g.Padding.y * scale;
            params.PaddingR = g.Padding.z * scale;
            params.PaddingB = g.Padding.w * scale;
            params.Spacing  = g.Spacing * scale;
            params.CellSize = g.CellSize * scale;
            params.Columns  = g.Columns;
            return MeasureLayoutGroup( params, sizes );
        }

        // Recursively draw one element. `forcedRect` (non-null) is the rect assigned by a parent auto-layout
        // group — it overrides the element's own anchors for position + size.
        void DrawElement( entt::registry& reg, entt::entity e, const Rect& parent, float scale,
                          Graphic::Render2D::DrawList2D& dl, const UIInput* input, std::string* outClicked,
                          entt::entity* focused, std::vector<PopupInfo>* popups,
                          std::vector<entt::entity>* focusables, const Rect* forcedRect = nullptr )
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
            if ( hasLayout ) // fitters reshape the resolved rect (also applied inside a layout group)
            {
                const auto& L = reg.get<ECS::UILayoutComponent>( e ).Data;
                rect          = ApplyAspectFit( rect, L.AspectRatio, static_cast<int>( L.AspectMode ) );
                if ( ( L.FitWidth || L.FitHeight ) && reg.has<ECS::UILayoutGroupComponent>( e ) )
                {
                    const glm::vec2 content = GroupContentPx( reg, e, scale );
                    if ( L.FitWidth )
                        rect.W = content.x;
                    if ( L.FitHeight )
                        rect.H = content.y;
                }
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
                    // Disabled swallows all pointer/keyboard interaction and rests on the dim colour.
                    const bool hover = !b.Disabled && input && input->MousePx.x >= mn.x &&
                                       input->MousePx.x <= mx.x && input->MousePx.y >= mn.y &&
                                       input->MousePx.y <= mx.y;
                    const bool down  = hover && input->MouseDown;
                    // Resting colour is Selected (persistent highlight) or Normal; hover cross-fades toward
                    // HoverColor (eased), press snaps to PressedColor, Disabled overrides everything.
                    const glm::vec3 rest = b.Selected ? b.SelectedColor : b.NormalColor;
                    const float     ht   = HoverEase( e, hover && !down );
                    const glm::vec3 c    = b.Disabled ? b.DisabledColor
                                           : down     ? b.PressedColor
                                                      : glm::mix( rest, b.HoverColor, ht );

                    // Image can change with state (hover / press), falling back to the normal Sprite.
                    Assets::AssetHandle spr = b.Sprite;
                    if ( down && HandleSet( b.PressedSprite ) )
                        spr = b.PressedSprite;
                    else if ( hover && HandleSet( b.HoverSprite ) )
                        spr = b.HoverSprite;
                    DrawBox( dl, mn, mx, glm::vec4( c, b.Disabled ? 0.6f : 1.0f ), spr, b.SpriteBorder, scale,
                             6.0f * scale );

                    // Selected accent: a rounded bar hugging the left edge (the "you are here" marker).
                    if ( b.Selected && !b.Disabled )
                    {
                        const float barW  = std::max( 2.0f, 3.0f * scale );
                        const float inset = 4.0f * scale;
                        dl.AddRectFilled( { mn.x, mn.y + inset }, { mn.x + barW, mx.y - inset },
                                          glm::vec4( b.SelectedAccent, 1.0f ), barW * 0.5f );
                    }

                    const bool isFocused = focused && *focused == e;
                    if ( outClicked && input && !b.Disabled &&
                         ( ( hover && input->MouseReleased ) || ( isFocused && input->Submit ) ) )
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

                    // Pulse breathes the whole panel's opacity between PulseMin and full (live dot / CTA glow).
                    const float op =
                         p.Pulse ? p.Opacity * ( p.PulseMin +
                                                 ( 1.0f - p.PulseMin ) *
                                                      ( 0.5f + 0.5f * std::sin( NowSeconds() * p.PulseSpeed ) ) )
                                 : p.Opacity;

                    if ( p.Glow && p.GlowSize > 0.0f )
                    {
                        const int   layers = 6;
                        const float gs     = p.GlowSize * scale;
                        for ( int i = 0; i < layers; ++i ) // large faint -> small; overlap into a soft glow
                        {
                            const float ex = gs * ( 1.0f - static_cast<float>( i ) / layers );
                            dl.AddRectFilled( { mn.x - ex, mn.y - ex }, { mx.x + ex, mx.y + ex },
                                              glm::vec4( p.GlowColor, 0.10f * op ), p.CornerRadius + ex );
                        }
                    }

                    if ( p.Shadow )
                        dl.AddRectFilled( { mn.x + p.ShadowOffset.x * scale, mn.y + p.ShadowOffset.y * scale },
                                          { mx.x + p.ShadowOffset.x * scale, mx.y + p.ShadowOffset.y * scale },
                                          glm::vec4( p.ShadowColor, op ), p.CornerRadius * scale );

                    // Circle forces full rounding (radius = half the shorter side) for avatars / badges / dots.
                    const float rounding = p.Circle ? std::min( rect.W, rect.H ) * 0.5f : p.CornerRadius * scale;

                    // A streamed video fills the panel (its stable texture is updated outside the pass by the
                    // VideoService); it takes precedence over the sprite/gradient fill while a path is set.
                    Graphic::Image2D* video = HandleSet( p.Video )
                                                   ? Runtime::ResourceRegistry::GetVideoService()->Resolve(
                                                          static_cast<uint64_t>( p.Video ) )
                                                   : nullptr;
                    if ( video )
                        dl.AddImage( video, mn, mx, { 0.0f, 0.0f }, { 1.0f, 1.0f }, glm::vec4( p.Color, op ) );
                    else if ( p.UseGradient && !HandleSet( p.Sprite ) )
                        dl.AddRectFilledMultiColor( mn, mx, glm::vec4( p.Color, op ),
                                                    glm::vec4( p.GradientColor, op ) );
                    else
                        DrawBox( dl, mn, mx, glm::vec4( p.Color, op ), p.Sprite, p.SpriteBorder, scale, rounding );

                    // Gradient ring hugging the edge (avatar / status / progress ring).
                    if ( p.RingWidth > 0.0f )
                    {
                        const glm::vec2 c      = ( mn + mx ) * 0.5f;
                        const float     outerR = std::min( rect.W, rect.H ) * 0.5f;
                        const float     rw     = std::max( 1.0f, p.RingWidth * scale );
                        dl.AddRing( c, outerR, outerR - rw, glm::vec4( p.RingColorA, 1.0f ),
                                    glm::vec4( p.RingColorB, 1.0f ) );
                    }

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
                    const bool isFocused = focused && *focused == e;
                    if ( input && ( ( hover && input->MouseReleased ) || ( isFocused && input->Submit ) ) )
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
                    const bool isFocused = focused && *focused == e;
                    if ( input && ( ( hover && input->MouseReleased ) || ( isFocused && input->Submit ) ) )
                        d.Open = !d.Open;
                    if ( d.Open && popups )
                        popups->push_back( { e, rect, scale } ); // defer the option list to draw on top
                }

                if ( reg.has<ECS::UITextComponent2D>( e ) )
                    DrawText2D( dl, reg.get<ECS::UITextComponent2D>( e ).Data, rect, scale );

                if ( reg.has<ECS::UIIconComponent>( e ) )
                {
                    const auto& ic = reg.get<ECS::UIIconComponent>( e ).Data;
                    DrawIcon( dl, ic.Icon, rect, glm::vec4( ic.Color, 1.0f ),
                              std::max( 1.0f, ic.Thickness * scale ), ic.Scale );
                }

                if ( reg.has<ECS::UIImageComponent>( e ) )
                {
                    // A sprite block — reuses DrawBox so it gets GIF playback, 9-slice and the static path.
                    // With no sprite bound it draws nothing (an empty Image is invisible, not a solid box).
                    const auto& im = reg.get<ECS::UIImageComponent>( e ).Data;
                    if ( HandleSet( im.Sprite ) )
                        DrawBox( dl, mn, mx, glm::vec4( im.Tint, im.Opacity ), im.Sprite, im.SpriteBorder, scale,
                                 0.0f );
                }

                // Keyboard focus: record this control for Tab-cycling, and draw a focus ring when it holds
                // focus (InputField draws its own coloured border, so skip the generic ring there).
                if ( IsFocusable( reg, e ) )
                {
                    if ( focusables )
                        focusables->push_back( e );
                    if ( focused && *focused == e && !reg.has<ECS::UIInputFieldComponent>( e ) )
                        dl.AddRect( mn, mx, glm::vec4( 0.30f, 0.62f, 0.98f, 1.0f ),
                                    std::max( 1.0f, 2.0f * scale ) );
                }
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
                    std::vector<float>        flex;
                    for ( auto c : children )
                    {
                        if ( !reg.valid( c ) )
                            continue;
                        glm::vec2 pref( 0.0f );
                        float     fg = 0.0f;
                        if ( reg.has<ECS::UILayoutComponent>( c ) )
                        {
                            const auto& L = reg.get<ECS::UILayoutComponent>( c ).Data;
                            pref          = glm::max( L.CustomMinimumSize, L.OffsetMax - L.OffsetMin );
                            fg            = L.FlexGrow;
                        }
                        kids.push_back( c );
                        sizes.push_back( pref * scale );
                        flex.push_back( fg );
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

                    const auto rects = SolveLayoutGroup( childParent, params, sizes, flex );
                    for ( std::size_t i = 0; i < kids.size(); ++i )
                        DrawElement( reg, kids[i], childParent, scale, dl, input, outClicked, focused, popups,
                                     focusables, &rects[i] );
                }
                else
                {
                    for ( auto c : children )
                        if ( reg.valid( c ) )
                            DrawElement( reg, c, childParent, scale, dl, input, outClicked, focused, popups,
                                         focusables );
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
        // Refresh the shared frame delta once per canvas draw (drives hover eases). Clamped so a long stall /
        // first frame doesn't snap animations.
        {
            static float lastT = NowSeconds();
            const float  now   = NowSeconds();
            s_FrameDt          = std::clamp( now - lastT, 0.0f, 0.1f );
            lastT              = now;
        }

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

        std::vector<PopupInfo>    popups;
        std::vector<entt::entity> focusables;
        if ( reg.has<ECS::RelationshipComponent>( canvasEntity ) )
            for ( auto c : reg.get<ECS::RelationshipComponent>( canvasEntity ).Children )
                if ( reg.valid( c ) )
                    DrawElement( reg, c, canvasRect, scale, dl, input, outClicked, focused, &popups, &focusables );

        // Tab advances keyboard focus to the next focusable control (wraps; effective next frame).
        if ( focused && input && input->Tab && !focusables.empty() )
        {
            std::size_t idx = 0; // not-found -> focus the first
            for ( std::size_t i = 0; i < focusables.size(); ++i )
                if ( focusables[i] == *focused )
                {
                    idx = i + 1;
                    break;
                }
            *focused = focusables[idx % focusables.size()];
        }

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
