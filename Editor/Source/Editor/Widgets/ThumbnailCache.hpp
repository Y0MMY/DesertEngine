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

        // Drop the cached entry for one path so the next Get() re-decodes it (used when a thumbnail PNG was
        // regenerated on disk). No-op if not cached.
        void Invalidate( const std::string& sourcePath );

        void Clear();

        // --- Shared rendered-thumbnail disk-cache layout (used by every panel that shows previews) ---------
        // Rendered material/mesh thumbnails live in a VERSIONED folder. The per-asset staleness check only
        // compares the source asset's modtime, so it can't notice when the thumbnail RENDERER improves — bump
        // CacheVersion() to invalidate every old thumbnail at once, and call PurgeOldVersions() once at startup
        // to delete the stale folders/files so they regenerate cleanly with the current renderer.
        static int         CacheVersion();
        static std::string DiskPath( const std::string& assetPath ); // versioned PNG path for an asset
        static void        PurgeOldVersions();                        // drop everything except the current version

    private:
        // Display cap: the on-disk PNG can be large (1024), but the grid shows it tiny, so decode it into a
        // small GPU texture (box-averaged downscale) to keep VRAM low. Storage res != display res.
        static constexpr int      kThumbMaxDim = 256;
        static constexpr std::size_t kMaxEntries = 512; // bound VRAM/handles

        std::unordered_map<std::string, std::shared_ptr<Graphic::Image2D>> m_Cache;
    };
} // namespace Desert::Editor
