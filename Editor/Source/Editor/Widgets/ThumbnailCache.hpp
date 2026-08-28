#pragma once

#include <Engine/Graphic/Image.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Desert::Editor
{
    // Decodes image files (any png/tga/jpg/hdr the asset browser shows) into small GPU textures for
    // thumbnails — independent of the cook pipeline, so EVERY image previews consistently (not only
    // already-cooked ones). Bounded + cached by source path; cleared on panel refresh.
    class ThumbnailCache
    {
    public:
        // Registers/unregisters with the live set that ReleaseAll() walks. Not copyable or movable: the set
        // holds raw pointers, and a copy would put an address in it that nothing owns.
        ThumbnailCache();
        ~ThumbnailCache();

        ThumbnailCache( const ThumbnailCache& )            = delete;
        ThumbnailCache& operator=( const ThumbnailCache& ) = delete;

        // Returns a cached thumbnail image for the source path (decoding + downscaling on first request),
        // or null if the file can't be decoded (caller falls back to an icon). Null results are cached too.
        std::shared_ptr<Graphic::Image2D> Get( const std::string& sourcePath );

        // Drop the cached entry for one path so the next Get() re-decodes it (used when a thumbnail PNG was
        // regenerated on disk). No-op if not cached.
        void Invalidate( const std::string& sourcePath );

        void Clear();

        /**
         * @brief Clear EVERY live cache's GPU images, while the device is still alive. Called from
         *        EditorLayer::OnDetach.
         *
         * WHY A STATIC SWEEP RATHER THAN A CALL PER OWNER. Most caches belong to a panel and go down with
         * `m_Panels.clear()`, which is safely inside the editor's teardown. Three do NOT: the component
         * widgets keep theirs in FUNCTION-STATICS —
         *
         *     StaticMeshComponent.cpp        `static MaterialComponentWidget materialComponent;`
         *     StaticMeshComponent.cpp        `static ThumbnailCache s_Thumbnails;`
         *     SkinnedMeshComponentWidget.cpp `static MaterialComponentWidget materials;`
         *
         * — so they are destroyed at `__cxa_finalize`, after ~Application has taken the device and the VMA
         * allocator with it. `~VulkanImage2D` then releases through a freed allocator and the process
         * segfaults. Measured: selecting a mesh with a material and quitting exited 139, with the backtrace
         * naming ~ThumbnailCache <- ~MaterialComponentWidget <- __cxa_finalize_ranges.
         *
         * This is the same family 0bfdeccf fixed for the engine-side registries, and the editor side had
         * FOUR members of it: ThumbnailService's renderer (released by its own Shutdown) and these three
         * image caches. A per-owner call would mean reaching into three widget files to add a hook each, and
         * would silently miss the fourth widget somebody writes next year. The cache knows its own
         * instances, so it is the thing that can promise all of them.
         *
         * Idempotent, and NOT a one-way switch — a cleared cache simply re-decodes on the next Get().
         *
         * Main thread only, like every other thumbnail path (decode and upload happen during the ImGui pass).
         */
        static void ReleaseAll();

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

        // Every constructed cache, so ReleaseAll() can reach the ones no panel owns. Raw pointers to
        // objects that deregister themselves; this set outlives them all and holds nothing that needs a
        // device, so its own static destruction is harmless.
        static std::unordered_set<ThumbnailCache*>& Live();
    };
} // namespace Desert::Editor
