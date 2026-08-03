#pragma once

#include <Engine/Graphic/Image.hpp>
#include <Engine/Text/FontBaker.hpp>

#include <Common/Core/AssetHandle.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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

        // --- Fonts as ASSETS (handle-referenced) --------------------------------------------------------
        // UI/text components reference a font by an AssetHandle, never a raw path: the user drags a .ttf from
        // the Content Browser (RegisterFont on drop) or picks a preloaded one (AvailableFonts). The handle is
        // AssetHandle::FromKey(path) — deterministic & path-derived, so the same .ttf always maps to the same
        // handle and a saved scene resolves without an import step. This registry (handle -> path) is the
        // single source of truth the editor picker, the (de)serializer and the render path all resolve through.

        // Record handle=FromKey(ttfPath) -> ttfPath and return the handle (idempotent). Assigned on drag-drop.
        uint64_t RegisterFont( const std::string& ttfPath );

        // Reverse lookup for display / serialization (path saved to disk keeps scenes portable). "" if unknown.
        std::string PathForHandle( uint64_t handle );

        // Resolve a font handle to its baked atlas (handle -> path -> Get(path,size)). nullptr if unregistered.
        Font* Get( uint64_t handle, float pixelHeight = 48.0f );

        // The built-in Roboto-Regular, registered on demand — the fallback when a component has no font set.
        uint64_t DefaultFontHandle();

        // Every registered .ttf path (scans the project + engine font roots once). Drives the picker dropdown.
        const std::vector<std::string>& AvailableFonts();

    private:
        // Scan ASSETS_PATH + FONTS_PATH for .ttf once, registering each — the "preloaded at startup" set.
        void EnsurePreloaded();

        std::unordered_map<std::string, std::unique_ptr<Font>> m_Fonts;        // key = path + '|' + size
        std::unordered_map<uint64_t, std::string>              m_HandleToPath; // font asset handle -> ttf path
        std::vector<std::string>                               m_Available;    // registered paths (for the picker)
        bool                                                   m_Scanned = false;
    };
} // namespace Desert::Runtime
