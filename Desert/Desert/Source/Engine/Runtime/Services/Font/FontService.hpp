#pragma once

#include <Engine/Graphic/Image.hpp>
#include <Engine/Text/FontBaker.hpp>

#include <memory>
#include <string>
#include <unordered_map>

namespace Desert::Runtime
{
    // A baked, GPU-resident font: the SDF atlas texture + the CPU glyph metrics that lay text out.
    struct Font
    {
        std::shared_ptr<Graphic::Image2D> Atlas; // RGBA8, SDF in every channel (shader reads .r)
        Text::BakedFont                   Baked; // metrics: glyph UVs/advances + vertical metrics
    };

    // Owns baked fonts keyed by (ttf path + bake pixel height). First request for a key reads the TTF
    // (VFS-aware), bakes the SDF atlas on the calling thread and uploads it once; later requests hit the
    // cache. The atlas Image2D stays alive for the service's lifetime, so render systems can hold a raw
    // pointer for the frame. Text is niche vs meshes/materials, so no lazy/eviction machinery yet.
    class FontService
    {
    public:
        // nullptr if the TTF can't be read or parsed (logged once).
        Font* Get( const std::string& ttfPath, float pixelHeight = 48.0f );
        void  Clear();

    private:
        std::unordered_map<std::string, std::unique_ptr<Font>> m_Fonts; // key = path + '|' + size
    };
} // namespace Desert::Runtime
