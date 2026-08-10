#pragma once

#include <Engine/Graphic/Clouds/CloudNoiseRules.hpp>
#include <Engine/Graphic/Image.hpp>

#include <cstdint>
#include <vector>

namespace Desert::Graphic
{
    // The three noise volumes the volumetric cloud passes sample. One set per distinct
    // CloudNoiseKey, shared by every scene that asks for that key and released when the last one
    // stops asking.
    struct CloudNoiseSet
    {
        Image3DRef ShapeNoise;  // 128^3 RGBA8F — R = Perlin-Worley base, GBA = Worley erosion octaves
        Image3DRef DetailNoise; // 32^3  RGBA8F — four Worley FBMs, low pair wispy, high pair billowy
        Image2DRef CurlNoise;   // 128^2 RGBA8F — RGB = encoded curl vector (see CloudNoise.glslh)

        [[nodiscard]] bool IsComplete() const
        {
            return ShapeNoise && DetailNoise && CurlNoise;
        }
    };

    /**
     * @brief Process-wide owner of the generated cloud noise volumes.
     *
     * Ownership model, and why it is keyed rather than a single global set: the resource table calls the
     * volumes "one per process, shared, refcounted". Literally one set per process only works while every
     * scene agrees on the seeds. The editor edits several scenes at once (Scenes -> New Scene View), so
     * two live scenes CAN want different seeds, and a single set would then either thrash — regenerating
     * 8 MiB twice per frame as the two scenes take turns — or silently ignore one scene's Shape Seed.
     * Keying by content and refcounting the key gives the intended sharing (same seeds => one set, one
     * generation) without either failure.
     *
     * Generation is an IMMEDIATE compute dispatch (ComputePipeline::Dispatch), which submits and waits.
     * That is the same path the IBL bake uses and it is correct here for the same reason: this is
     * one-shot work at a frame boundary, not per-frame work, and the volumes must be finished before the
     * first raymarch that samples them.
     *
     * Failure is LATCHED per key: a set that could not be built is not retried every frame (a missing
     * shader would otherwise cost a full dispatch attempt and a log line at 60 Hz). The component's
     * transient RequestRegenerateNoise is what clears the latch and tries again.
     *
     * Not thread-safe by design: every entry point creates or destroys GPU resources, so it is called
     * from the scene/render thread only — ECS::CloudNoiseECSSystem does not opt into parallel execution.
     */
    class CloudNoiseVolumes final
    {
    public:
        static CloudNoiseVolumes& Get();

        CloudNoiseVolumes( const CloudNoiseVolumes& )            = delete;
        CloudNoiseVolumes& operator=( const CloudNoiseVolumes& ) = delete;

        /**
         * @brief Take a lease on the set for @p key, generating it if this is the first lease.
         *
         * @param forceRegenerate Discard whatever exists for this key (including a latched failure) and
         *                        generate again. This is the component's RequestRegenerateNoise.
         *
         * Every Acquire must be matched by exactly one Release with the SAME key.
         */
        void Acquire( const CloudNoiseKey& key, bool forceRegenerate );

        /** @brief Drop one lease. The volumes are destroyed when the last lease on @p key goes. */
        void Release( const CloudNoiseKey& key );

        /**
         * @brief The volumes generated for @p key, or nullptr when nothing holds a lease on it or the
         *        generation failed. The pointer stays valid until the last lease on @p key is released.
         */
        [[nodiscard]] const CloudNoiseSet* Find( const CloudNoiseKey& key ) const;

        /** @brief How many distinct keys are live. Diagnostics, and the hook the shutdown check uses. */
        [[nodiscard]] size_t LiveSetCount() const
        {
            return m_Entries.size();
        }

    private:
        CloudNoiseVolumes() = default;

        struct Entry
        {
            CloudNoiseKey Key{};
            uint32_t      LeaseCount = 0;
            CloudNoiseSet Volumes{};
            // Generation failed for this key and will not be retried until a forced regeneration. The
            // reason was logged once, at the failure.
            bool Failed = false;
        };

        Entry*       FindEntry( const CloudNoiseKey& key );
        const Entry* FindEntry( const CloudNoiseKey& key ) const;

        // Builds the three volumes for @p key. Leaves Volumes empty and Failed set on any failure, having
        // logged which stage failed and with what dimensions.
        void Generate( Entry& entry );

        // A vector, not a map: the number of distinct keys is the number of distinct seed pairs across
        // live scenes, which is one in every scene we ship and a handful in the worst editor session. A
        // linear scan over that is cheaper than hashing, and it keeps the iteration order stable.
        std::vector<Entry> m_Entries;
    };
} // namespace Desert::Graphic
