#pragma once

#include <Engine/Graphic/Clouds/CloudVolumeAtlasLayout.hpp>
#include <Engine/Graphic/Image.hpp>

#include <Common/Core/ResultStr.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace Desert::Graphic
{
    /**
     * @brief The GPU home of the placed hero clouds: eight `.dvol` tiles inside one 3D image.
     *
     * Ownership and lifetime follow Graphic::CloudNoiseVolumes — keyed entries, refcounted leases, a
     * failure latched per atlas so a broken bake is not retried at 60 Hz, and one MiB line in the log so
     * the cost is visible without a memory graph. It differs from that class in one deliberate way: it
     * is NOT a singleton. The noise set is genuinely process-wide (several scenes share one set keyed by
     * seed); a hero-cloud atlas belongs to the renderer that marches it, so it is a plain member and its
     * dependency is passed rather than reached for.
     *
     * WHY A CPU COPY OF THE WHOLE ATLAS IS RETAINED. `Image3DSpecification` uploads its pixels once, at
     * creation, and the engine has no partial-region volume upload. Adding one would mean a second
     * `VkBufferImageCopy` path in `VulkanImage3D` with its own barriers — real work, and work whose only
     * customer today is a tile change that happens when a scene loads or an artist drags an asset, not
     * per frame. So the atlas keeps its 32 MiB of pixels host-side and rebuilds the image when the
     * resident set changes. That is 32 MiB of RAM to avoid a piece of backend machinery nobody has
     * measured a need for; if tile churn ever shows up in a profile, the fix is the partial upload, and
     * this comment is where to start.
     *
     * Not thread-safe: every entry point can create or destroy a GPU image, so it is called from the
     * render thread only.
     */
    class CloudVolumeAtlas final
    {
    public:
        explicit CloudVolumeAtlas( const CloudVolumeAtlasLayout& layout = CloudVolumeAtlasLayout{} );

        CloudVolumeAtlas( const CloudVolumeAtlas& )            = delete;
        CloudVolumeAtlas& operator=( const CloudVolumeAtlas& ) = delete;

        /**
         * @brief Take a lease on a tile holding @p volume, uploading it if this is the first lease.
         *
         * @param key    Identifies the CONTENT, not the placement — the `.dvol` asset handle. Two
         *               entities that reference the same volume share one tile, which is the whole point
         *               of instancing (deck p. 185: "Cinematics Memory benefits from Re-use").
         * @return       The tile index to put in the instance record, or the reason there is none.
         *
         * Every Acquire must be matched by exactly one Release with the SAME key.
         */
        Common::ResultStr<uint32_t> Acquire( uint64_t key, const CloudVolume& volume );

        /** @brief Drop one lease. The tile becomes free when the last lease on @p key goes. */
        void Release( uint64_t key );

        /** @brief The tile @p key occupies, or nothing when no lease is held on it. */
        [[nodiscard]] std::optional<uint32_t> Find( uint64_t key ) const;

        /** @brief The atlas image, or nullptr before the first successful upload. */
        [[nodiscard]] const Image3DRef& GetImage() const
        {
            return m_Image;
        }

        [[nodiscard]] const CloudVolumeAtlasLayout& GetLayout() const
        {
            return m_Layout;
        }

        [[nodiscard]] size_t TilesInUse() const;

    private:
        struct Tile
        {
            uint64_t Key        = 0; // the .dvol asset handle; 0 = free
            uint32_t LeaseCount = 0;
        };

        // Rebuilds the image from m_Pixels. Waits for device idle first: the previous image may still be
        // bound by a frame in flight, exactly as the noise set and the sky environment bake do.
        Common::BoolResultStr Upload();

        CloudVolumeAtlasLayout     m_Layout;
        std::vector<Tile>          m_Tiles;
        std::vector<unsigned char> m_Pixels;
        Image3DRef                 m_Image;

        // Latched after a failed upload so the next frame does not attempt the same 32 MiB allocation
        // again. Cleared whenever the resident set actually changes, because a different set is a
        // different attempt rather than a repeat of the one that failed.
        bool m_Failed = false;
    };
} // namespace Desert::Graphic
