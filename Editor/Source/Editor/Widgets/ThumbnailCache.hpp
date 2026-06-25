#pragma once

#include <Engine/Graphic/Image.hpp>

#include <memory>
#include <string>
#include <unordered_map>

namespace Desert::Editor
{
    // Decodes image files (any png/tga/jpg/hdr the asset browser shows) into small GPU textures for
    // thumbnails — independent of the cook pipeline, so EVERY image previews consistently (not only
    // already-cooked ones). Bounded + cached by source path; cleared on panel refresh.
    class ThumbnailCache
    {
    public:
        // Returns a cached thumbnail image for the source path (decoding + downscaling on first request),
        // or null if the file can't be decoded (caller falls back to an icon). Null results are cached too.
        std::shared_ptr<Graphic::Image2D> Get( const std::string& sourcePath );

        void Clear();

    private:
        static constexpr int      kThumbMaxDim = 128;
        static constexpr std::size_t kMaxEntries = 512; // bound VRAM/handles

        std::unordered_map<std::string, std::shared_ptr<Graphic::Image2D>> m_Cache;
    };
} // namespace Desert::Editor
