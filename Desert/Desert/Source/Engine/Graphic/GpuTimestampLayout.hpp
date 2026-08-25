#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Desert::Graphic
{
    // ------------------------------------------------------------------------------------------------
    // Where each GPU timestamp lives in the query pool, and how a nested measurement is turned into a
    // breakdown that adds up. Pure functions of integers, outside the Vulkan class that uses them, for
    // the same reason RenderGraphSort.hpp sits outside RenderGraphBuilder: VulkanGpuProfiler cannot be
    // constructed without a device, so none of this would otherwise be assertable.
    //
    // Two relations live here, and both have already been wrong once:
    //
    //   1. A QUERY BELONGS TO EXACTLY ONE (frame x renderer slot). Docs/RENDERER_FRAME_STATE.md is the
    //      history of what happens when a per-frame GPU resource forgets the second dimension: the editor
    //      runs several live SceneRenderers into one command buffer, and a pool keyed by frame alone has
    //      the asset preview's "VolumetricClouds" land on top of the viewport's. Tests/Engine/
    //      GpuTimestampLayout asserts the ranges are disjoint rather than trusting the multiplication.
    //
    //   2. SELF TIMES PARTITION THE ROOT. Passes nest — the cloud march sits inside "Clouds:
    //      ExecuteInFrame" inside "VolumetricClouds" — so adding up the inclusive times counts the same
    //      microseconds three times. The first breakdown ever printed by this feature summed to 159% of
    //      its own frame for exactly that reason. Subtracting each scope's DIRECT children makes the
    //      remainder a partition, and that is a property worth asserting rather than eyeballing.
    // ------------------------------------------------------------------------------------------------

    /// Queries reserved for one (frame x slot): two per scope, begin and end.
    [[nodiscard]] constexpr uint32_t GpuQueriesPerSlot( uint32_t maxScopesPerSlot )
    {
        return maxScopesPerSlot * 2;
    }

    /// Queries reserved for one frame: every slot, plus the pair that brackets the whole command buffer.
    [[nodiscard]] constexpr uint32_t GpuQueriesPerFrame( uint32_t slotCount, uint32_t maxScopesPerSlot )
    {
        return slotCount * GpuQueriesPerSlot( maxScopesPerSlot ) + 2;
    }

    /// First query of a (frame x slot) block.
    [[nodiscard]] constexpr uint32_t GpuSlotQueryBase( uint32_t frameIndex, uint32_t slot, uint32_t slotCount,
                                                       uint32_t maxScopesPerSlot )
    {
        return frameIndex * GpuQueriesPerFrame( slotCount, maxScopesPerSlot ) +
               slot * GpuQueriesPerSlot( maxScopesPerSlot );
    }

    /// The whole-frame bracket's pair, which sits after every slot's block in the same frame.
    [[nodiscard]] constexpr uint32_t GpuFrameTotalQueryBase( uint32_t frameIndex, uint32_t slotCount,
                                                             uint32_t maxScopesPerSlot )
    {
        return frameIndex * GpuQueriesPerFrame( slotCount, maxScopesPerSlot ) +
               slotCount * GpuQueriesPerSlot( maxScopesPerSlot );
    }

    /// Which frame a query index belongs to. The inverse of GpuSlotQueryBase's frame term — a scope
    /// closes against the state it opened against by decoding its own handle, so this must invert.
    [[nodiscard]] constexpr uint32_t GpuDecodeFrame( uint32_t queryBase, uint32_t slotCount,
                                                     uint32_t maxScopesPerSlot )
    {
        return queryBase / GpuQueriesPerFrame( slotCount, maxScopesPerSlot );
    }

    /// Which renderer slot a query index belongs to. Frame-total queries decode to slotCount, which is
    /// out of range on purpose: they belong to no renderer, and a caller must not treat them as slot 0's.
    [[nodiscard]] constexpr uint32_t GpuDecodeSlot( uint32_t queryBase, uint32_t slotCount,
                                                    uint32_t maxScopesPerSlot )
    {
        return ( queryBase % GpuQueriesPerFrame( slotCount, maxScopesPerSlot ) ) /
               GpuQueriesPerSlot( maxScopesPerSlot );
    }

    /// A scope with no enclosing scope.
    inline constexpr int32_t kGpuNoParent = -1;

    // Turn inclusive per-scope times into EXCLUSIVE ones by subtracting each scope's direct children.
    //
    // @p inclusiveMs is indexed by scope; a negative entry marks a scope whose queries did not both land
    // and is passed through untouched (and contributes nothing to its parent). @p parents holds each
    // scope's parent index or kGpuNoParent, and must refer only to EARLIER indices — scopes are recorded
    // in the order they open, so a parent always precedes its children.
    //
    // The relation this exists for: for any tree whose children lie inside their parents, the self times
    // sum to the root's inclusive time. Nothing else in the breakdown may be summed.
    [[nodiscard]] inline std::vector<double> GpuSelfTimes( const std::vector<double>&  inclusiveMs,
                                                           const std::vector<int32_t>& parents )
    {
        std::vector<double> self = inclusiveMs;

        const size_t count = inclusiveMs.size() < parents.size() ? inclusiveMs.size() : parents.size();
        for ( size_t i = 0; i < count; ++i )
        {
            const int32_t parent = parents[i];
            if ( inclusiveMs[i] < 0.0 || parent == kGpuNoParent )
                continue;
            if ( static_cast<size_t>( parent ) < count && inclusiveMs[parent] >= 0.0 )
                self[parent] -= inclusiveMs[i];
        }

        // A parent whose children overlap it imperfectly can round below zero. Report zero rather than a
        // negative time: a negative would silently shrink the total and read as an unexplained gap.
        for ( size_t i = 0; i < self.size(); ++i )
            if ( inclusiveMs[i] >= 0.0 && self[i] < 0.0 )
                self[i] = 0.0;

        return self;
    }
} // namespace Desert::Graphic
