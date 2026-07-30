#include "FontBaker.hpp"

#include <stb_truetype/stb_truetype.h>

#include <algorithm>
#include <cstdlib>

namespace Desert::Text
{
    namespace
    {
        // One rasterized glyph before packing: its SDF bitmap (owned by stb, freed after copy) + metrics.
        struct RawGlyph
        {
            uint32_t       Codepoint = 0;
            unsigned char* Bitmap    = nullptr; // stb-allocated (STBTT_free); w*h single channel
            int            W = 0, H = 0;
            int            XOff = 0, YOff = 0; // glyph top-left relative to the pen baseline (Y down)
            float          Advance = 0;        // scaled pixels
        };
    } // namespace

    BakedFont BakeFontSDF( const uint8_t* ttf, size_t ttfSize, float pixelHeight, int padding,
                           uint32_t atlasWidth )
    {
        BakedFont out;
        if ( !ttf || ttfSize == 0 || pixelHeight <= 0.0f || atlasWidth < 32 )
            return out;

        stbtt_fontinfo font;
        // stbtt_GetFontOffsetForIndex returns -1 for non-font data. stbtt_InitFont does NOT re-check it and
        // stbtt__find_table would then read the table directory at (data - 1 + ...) — a heap-buffer-overflow
        // on garbage/too-short input (ASan caught this on the RejectsGarbage test). Reject a bad offset first.
        const int fontOffset = stbtt_GetFontOffsetForIndex( ttf, 0 );
        if ( fontOffset < 0 || !stbtt_InitFont( &font, ttf, fontOffset ) )
            return out;

        const float scale = stbtt_ScaleForPixelHeight( &font, pixelHeight );

        int ascent = 0, descent = 0, lineGap = 0;
        stbtt_GetFontVMetrics( &font, &ascent, &descent, &lineGap );
        out.PixelHeight = pixelHeight;
        out.Ascent      = ascent * scale;
        out.Descent     = descent * scale;
        out.LineGap     = lineGap * scale;

        // onedge/pixel_dist_scale place the glyph edge at kSdfOnEdgeValue and spread `padding` pixels of
        // distance across the full byte range — enough gradient for smooth AA + a bit of outline room.
        const float pixelDistScale = static_cast<float>( kSdfOnEdgeValue ) / static_cast<float>( padding );

        std::vector<RawGlyph> raws;
        raws.reserve( 95 );
        for ( uint32_t cp = 32; cp <= 126; ++cp )
        {
            int advance = 0, lsb = 0;
            stbtt_GetCodepointHMetrics( &font, static_cast<int>( cp ), &advance, &lsb );

            RawGlyph rg;
            rg.Codepoint = cp;
            rg.Advance   = advance * scale;

            // Space (and any empty glyph) has no bitmap — keep it for its advance only.
            int w = 0, h = 0, xoff = 0, yoff = 0;
            unsigned char* bmp = stbtt_GetCodepointSDF( &font, scale, static_cast<int>( cp ), padding,
                                                        kSdfOnEdgeValue, pixelDistScale, &w, &h, &xoff, &yoff );
            rg.Bitmap = bmp;
            rg.W = w;
            rg.H = h;
            rg.XOff = xoff;
            rg.YOff = yoff;
            raws.push_back( rg );
        }

        // Shelf pack: left-to-right rows of fixed atlas width, wrap to a new shelf when the row is full.
        const uint32_t spacing = 1; // 1px gutter so bilinear sampling never bleeds a neighbour
        uint32_t       penX = spacing, penY = spacing, shelfH = 0, usedH = 0;
        struct Placed { uint32_t X, Y; const RawGlyph* G; };
        std::vector<Placed> placed;
        placed.reserve( raws.size() );

        for ( const auto& rg : raws )
        {
            if ( rg.W <= 0 || rg.H <= 0 )
                continue; // empty glyph (space) — no atlas cell needed
            const uint32_t gw = static_cast<uint32_t>( rg.W );
            const uint32_t gh = static_cast<uint32_t>( rg.H );
            if ( penX + gw + spacing > atlasWidth )
            {
                penX   = spacing;
                penY  += shelfH + spacing;
                shelfH = 0;
            }
            placed.push_back( { penX, penY, &rg } );
            penX  += gw + spacing;
            shelfH = std::max( shelfH, gh );
            usedH  = std::max( usedH, penY + gh + spacing );
        }

        out.AtlasWidth  = atlasWidth;
        out.AtlasHeight = std::max<uint32_t>( usedH, 1 );
        out.AtlasR8.assign( static_cast<size_t>( out.AtlasWidth ) * out.AtlasHeight, 0 );

        const auto invW = 1.0f / static_cast<float>( out.AtlasWidth );
        const auto invH = 1.0f / static_cast<float>( out.AtlasHeight );

        // Copy each packed bitmap into the atlas and record UVs + metrics.
        for ( const auto& p : placed )
        {
            const RawGlyph& rg = *p.G;
            for ( int y = 0; y < rg.H; ++y )
                for ( int x = 0; x < rg.W; ++x )
                    out.AtlasR8[static_cast<size_t>( p.Y + y ) * out.AtlasWidth + ( p.X + x )] =
                         rg.Bitmap[y * rg.W + x];

            Glyph g;
            g.U0      = p.X * invW;
            g.V0      = p.Y * invH;
            g.U1      = ( p.X + rg.W ) * invW;
            g.V1      = ( p.Y + rg.H ) * invH;
            g.Width   = static_cast<float>( rg.W );
            g.Height  = static_cast<float>( rg.H );
            g.OffsetX = static_cast<float>( rg.XOff );
            g.OffsetY = static_cast<float>( rg.YOff );
            g.Advance = rg.Advance;
            out.Glyphs[rg.Codepoint] = g;
        }

        // Empty glyphs (space) carry only an advance so layout can step past them.
        for ( const auto& rg : raws )
        {
            if ( out.Glyphs.count( rg.Codepoint ) )
                continue;
            Glyph g;
            g.Advance                = rg.Advance;
            out.Glyphs[rg.Codepoint] = g;
        }

        for ( auto& rg : raws )
            if ( rg.Bitmap )
                stbtt_FreeSDF( rg.Bitmap, nullptr );

        return out;
    }
} // namespace Desert::Text
