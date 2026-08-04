#pragma once

#include <Engine/Graphic/Image.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Desert::Runtime
{
    // A baked icon: the SDF texture plus the sub-rect it occupies. Icons are drawn by the TEXT shader —
    // an SDF is an SDF — so they stay crisp at any size and inherit outline / glow / shadow for free.
    struct Icon
    {
        std::shared_ptr<Graphic::Image2D> Atlas;                // RGBA8, SDF in every channel
        float                             U0 = 0.0f, V0 = 0.0f; // sub-rect (a shared atlas can come
        float                             U1 = 1.0f, V1 = 1.0f; // later without touching callers)
        float                             Aspect = 1.0f;        // source viewBox width / height
    };

    // Owns icons baked from .svg, keyed by asset handle. Mirrors FontService on purpose: the handle is
    // AssetHandle::FromKey(path) — deterministic and path-derived, so the same file always maps to the
    // same handle and a saved scene resolves with no import step. SVG is parsed HERE, at import time
    // only; the runtime draw path never sees XML.
    class IconService
    {
    public:
        // Record handle=FromKey(svgPath) -> svgPath and return the handle (idempotent). Assigned on drop.
        uint64_t RegisterIcon( const std::string& svgPath );

        // Reverse lookup for display / serialization ("" if the handle is unknown).
        std::string PathForHandle( uint64_t handle );

        // Resolve a handle to its baked SDF (bakes + uploads on first use). nullptr if unregistered or
        // the file cannot be parsed — a broken icon draws nothing rather than taking the frame down.
        Icon* Get( uint64_t handle );

        // Every registered .svg path (scans the project + engine icon roots once). Drives the picker.
        const std::vector<std::string>& AvailableIcons();

        void Clear();

    private:
        void EnsurePreloaded();

        std::unordered_map<std::string, std::unique_ptr<Icon>> m_Icons;        // svg path -> baked icon
        std::unordered_map<uint64_t, std::string>              m_HandleToPath; // asset handle -> svg path
        std::vector<std::string>                               m_Available;    // registered paths
        bool                                                   m_Scanned = false;
    };
} // namespace Desert::Runtime
