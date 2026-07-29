#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Pure CPU font baking: TrueType bytes -> a single-channel SDF glyph atlas + per-glyph metrics.
// Depends ONLY on stb_truetype + the standard library (no engine/GPU types) so it is unit-testable
// in isolation. The engine-side FontService turns a BakedFont into a GPU texture + draws quads.
namespace Desert::Text
{
    // One glyph in the baked atlas. UVs are normalized [0,1]; the pixel-space fields are relative to
    // the SDF bake size (PixelHeight) and scale linearly with the rendered text size.
    struct Glyph
    {
        float U0 = 0, V0 = 0, U1 = 0, V1 = 0; // atlas sub-rect (normalized)
        float Width = 0, Height = 0;          // glyph quad size in bake pixels (SDF bitmap, incl. padding)
        float OffsetX = 0, OffsetY = 0;       // pen -> glyph top-left, in bake pixels (Y down)
        float Advance = 0;                    // horizontal pen advance in bake pixels
    };

    struct BakedFont
    {
        uint32_t                            AtlasWidth  = 0;
        uint32_t                            AtlasHeight = 0;
        std::vector<uint8_t>                AtlasR8; // AtlasWidth*AtlasHeight, one SDF byte per texel
        std::unordered_map<uint32_t, Glyph> Glyphs;  // keyed by Unicode codepoint

        float PixelHeight = 0;         // the bake size the metrics are expressed in
        float Ascent = 0, Descent = 0; // scaled to bake pixels (Descent is negative, Y-down)
        float LineGap = 0;

        bool Valid() const { return AtlasWidth > 0 && AtlasHeight > 0 && !AtlasR8.empty(); }
        float LineHeight() const { return Ascent - Descent + LineGap; }
    };

    // SDF edge lives at this texel value (matches the text shader's smoothstep centre, 128/255 ~ 0.5).
    inline constexpr unsigned char kSdfOnEdgeValue = 128;

    // Bakes printable ASCII [32,126] into an SDF atlas via a simple shelf packer. `pixelHeight` is the
    // SDF bake resolution (bigger = sharper minification headroom, larger atlas). Returns an invalid
    // BakedFont (Valid() == false) if the TTF cannot be parsed. Never throws.
    BakedFont BakeFontSDF( const uint8_t* ttf, size_t ttfSize, float pixelHeight = 48.0f,
                           int padding = 5, uint32_t atlasWidth = 512 );
} // namespace Desert::Text
