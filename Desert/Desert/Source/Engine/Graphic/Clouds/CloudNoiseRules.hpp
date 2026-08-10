#pragma once

#include <Engine/Core/Formats/ImageFormat.hpp>

#include <cstdint>

// Pure rules for the cloud noise volumes: their fixed dimensions, the key that decides what their
// CONTENT is, and the decision of what to do about a scene that wants them. Nothing here touches the
// GPU, the registry or a file, so all of it is unit-testable without Vulkan — which, in this
// environment, is the only kind of verification there is (DEV_CONTRACT 2.3).
//
// The GPU-owning side is Graphic::CloudNoiseVolumes; the scene-facing side is
// ECS::CloudNoiseECSSystem. Both take their decisions from this header rather than reimplementing them.

namespace Desert::Graphic
{
    // ---- Dimensions -------------------------------------------------------------------------------
    //
    // Fixed by the resource table of the cloud requirements, not by a setting: these sizes are a
    // memory/quality trade-off that was made once, and a slider on them would change the meaning of
    // every tile-size and frequency in the raymarch.

    inline constexpr uint32_t kCloudShapeNoiseSize  = 128; // 128^3 RGBA8F = 8 MiB
    inline constexpr uint32_t kCloudDetailNoiseSize = 32;  // 32^3  RGBA8F = 128 KiB
    inline constexpr uint32_t kCloudCurlNoiseSize   = 128; // 128^2 RGBA8F = 64 KiB

    inline constexpr Core::Formats::ImageFormat kCloudNoiseFormat = Core::Formats::ImageFormat::RGBA8F;

    // Work-group edge of the generation shaders. 8x8x8 = 512 invocations is the group size every Vulkan
    // implementation is required to support, and all three volume edges divide by 8 so no shader needs a
    // bounds test that would be dead in every configuration we ship.
    inline constexpr uint32_t kCloudNoiseWorkGroupSize = 8;

    static_assert( kCloudShapeNoiseSize % kCloudNoiseWorkGroupSize == 0 );
    static_assert( kCloudDetailNoiseSize % kCloudNoiseWorkGroupSize == 0 );
    static_assert( kCloudCurlNoiseSize % kCloudNoiseWorkGroupSize == 0 );

    // Total GPU cost of one shared set. Logged when a set is generated so the number is visible in the
    // log rather than discovered in a memory graph.
    inline constexpr uint64_t CloudNoiseSetBytes()
    {
        return Core::Formats::CalculateImageSize( kCloudShapeNoiseSize, kCloudShapeNoiseSize,
                                                  kCloudShapeNoiseSize, kCloudNoiseFormat ) +
               Core::Formats::CalculateImageSize( kCloudDetailNoiseSize, kCloudDetailNoiseSize,
                                                  kCloudDetailNoiseSize, kCloudNoiseFormat ) +
               Core::Formats::CalculateImageSize( kCloudCurlNoiseSize, kCloudCurlNoiseSize,
                                                  kCloudNoiseFormat );
    }

    // ---- Seeds ------------------------------------------------------------------------------------

    // The component authors seeds as `int` with a Range(0, 65535). The generator hashes `uint`, and a
    // value outside the range would otherwise sign-extend into a different volume than the one the
    // Details panel can reach. Fold it explicitly instead: an out-of-range seed is a legal seed, it is
    // simply the same one as its representative in [0, 65535].
    inline constexpr uint32_t kCloudSeedPeriod = 65536u;

    inline constexpr uint32_t CloudSeedFromComponent( int authored )
    {
        // Euclidean remainder: -1 must fold to 65535, not to -1 reinterpreted as 4294967295.
        const int64_t folded = static_cast<int64_t>( authored ) % static_cast<int64_t>( kCloudSeedPeriod );
        return static_cast<uint32_t>( folded < 0 ? folded + kCloudSeedPeriod : folded );
    }

    // The curl map has no seed of its own in the component, and it should not grow one: curl warps the
    // DETAIL lookup, so the two must reshuffle together or changing Detail Seed would leave the swirls
    // of the previous cloudscape stamped over the new one. Derived, not authored — and derived through a
    // constant that puts it far from the detail volume's own channel seeds so the two do not correlate.
    inline constexpr uint32_t CloudCurlSeedFrom( uint32_t detailSeed )
    {
        return detailSeed + 3001u;
    }

    // ---- The key ----------------------------------------------------------------------------------

    // Everything the CONTENT of the three volumes depends on. Deliberately NOT in here:
    //
    //   * ShapeTileSize / DetailTileSize / CurlTileSize. Those are the WORLD size one tile covers, i.e.
    //     a scale applied when the raymarch samples the volume. A tiling volume tiles at any world
    //     scale, so regenerating on a tile-size drag would rebuild 8 MiB to produce the identical bytes.
    //   * The per-channel lattice frequencies. They are fixed constants of the generator (the cloud
    //     requirements exclude raw frequencies from the exposed set on purpose), so they cannot vary at
    //     runtime and have nothing to key on.
    struct CloudNoiseKey
    {
        uint32_t ShapeSeed  = 0;
        uint32_t DetailSeed = 0;
    };

    inline constexpr bool operator==( const CloudNoiseKey& a, const CloudNoiseKey& b )
    {
        return a.ShapeSeed == b.ShapeSeed && a.DetailSeed == b.DetailSeed;
    }

    inline constexpr bool operator!=( const CloudNoiseKey& a, const CloudNoiseKey& b )
    {
        return !( a == b );
    }

    inline constexpr CloudNoiseKey MakeCloudNoiseKey( int authoredShapeSeed, int authoredDetailSeed )
    {
        return CloudNoiseKey{ CloudSeedFromComponent( authoredShapeSeed ),
                              CloudSeedFromComponent( authoredDetailSeed ) };
    }

    // ---- The lifecycle decision -------------------------------------------------------------------

    // What a subscriber currently holds.
    struct CloudNoiseLease
    {
        bool          Held = false; // this subscriber holds a lease right now
        CloudNoiseKey Key{};        // the key it was taken on; meaningless when !Held
    };

    // What the scene asks for this frame.
    struct CloudNoiseDemand
    {
        bool          Wanted = false; // a volumetric-clouds component exists and is enabled
        CloudNoiseKey Key{};
        // The component's transient RequestRegenerateNoise. It matters even though the content is a pure
        // function of the seed: generation can FAIL (no shader, no memory), and the failure is latched so
        // it is not retried every frame. This flag is what clears the latch and tries again — the editor
        // button has a job, it is not a decorative "re-bake identical bytes".
        bool ForceRegenerate = false;
    };

    enum class CloudNoiseAction
    {
        None,       // the lease already matches the demand
        Generate,   // take a lease (the set is built if nobody else holds one on this key)
        Regenerate, // drop the current lease and take one on the new key / after a forced retry
        Release     // nothing wants the volumes any more
    };

    inline constexpr CloudNoiseAction DecideCloudNoiseAction( const CloudNoiseLease&  lease,
                                                              const CloudNoiseDemand& demand )
    {
        if ( !demand.Wanted )
            return lease.Held ? CloudNoiseAction::Release : CloudNoiseAction::None;

        if ( !lease.Held )
            return CloudNoiseAction::Generate;

        if ( lease.Key != demand.Key || demand.ForceRegenerate )
            return CloudNoiseAction::Regenerate;

        return CloudNoiseAction::None;
    }
} // namespace Desert::Graphic
