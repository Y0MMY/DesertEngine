#pragma once

#include <cstdint>

namespace Desert::ShaderResources
{
    // ------------------------------------------------------------------------------------------------
    // Where a shader-visible buffer's copies live, and which one a given (frame x renderer slot) reads.
    //
    // Pure integer functions, outside the Vulkan classes that use them, for the reason
    // GpuTimestampLayout.hpp gives: VulkanUniformBuffer and VulkanStorageBuffer cannot be constructed
    // without a device, so this arithmetic would otherwise be unassertable — and it was written out by
    // hand in BOTH of them, with nothing checking that the two agreed. Docs/RENDERER_FRAME_STATE.md is
    // the record of what a per-frame GPU resource costs when it forgets the slot dimension.
    //
    // Two relations live here:
    //
    //   1. DISTINCT (frame, slot) PAIRS NEVER SHARE A COPY. This is the whole point of the second
    //      dimension: the editor records several live SceneRenderers into one command buffer, and a
    //      buffer keyed by frame alone has a preview's camera land on top of the viewport's.
    //
    //   2. EVERY COPY IS REACHABLE, AND NOTHING BEYOND THE ALLOCATION IS. A write resolves the copy for
    //      the recording pair; the descriptor that points at it resolves the same way. If the count and
    //      the index disagree the mismatch is a wrong picture, not a crash — the allocation is
    //      zero-filled, so an unwritten copy renders BLACK rather than as garbage, which is precisely
    //      how the shader-graph parameter defect looked from the outside.
    // ------------------------------------------------------------------------------------------------

    /// How many copies a buffer must allocate to give every (frame x slot) pair its own.
    [[nodiscard]] constexpr uint32_t BufferCopyCount( uint32_t frameCount, uint32_t slotCount )
    {
        return frameCount * slotCount;
    }

    /// The copy belonging to one (frame x slot). Copies are laid out frame-major: [frame][slot].
    /// A slot at or past @p slotCount folds onto 0 — the same fallback SceneRenderer takes when it runs
    /// out of leases, so a stray slot shares the main view's copy instead of indexing out of bounds.
    [[nodiscard]] constexpr uint32_t BufferCopyIndex( uint32_t frameIndex, uint32_t slot, uint32_t slotCount )
    {
        return frameIndex * slotCount + ( slot < slotCount ? slot : 0u );
    }
} // namespace Desert::ShaderResources
