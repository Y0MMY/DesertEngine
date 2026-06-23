#pragma once

#include <Engine/Core/FrameManager.hpp>

#include <cstdint>
#include <limits>

namespace Desert::Graphic
{
    // Shared helpers for the material-property "dirty" mechanism.
    //
    // Material uniform buffers and their descriptor sets are replicated per frame-in-flight. When a
    // value changes, the corresponding descriptor set / GPU buffer for EACH frame-in-flight must be
    // refreshed exactly once. A property therefore stays dirty for "frames-in-flight" distinct frames
    // and is cleaned at most once per frame. Getting this wrong leaves some frame's descriptor set
    // pointing at the uninitialized fallback buffer, which renders as garbage (white/NaN) on the
    // frames that index is presented.
    namespace PropertyDirty
    {
        inline constexpr uint64_t kNeverCleaned = std::numeric_limits<uint64_t>::max();

        // Number of frames a property must remain dirty so every per-frame-in-flight resource is
        // updated once. Falls back to a safe value before the swap chain (and FrameManager) exist.
        inline uint32_t DirtyLifetime()
        {
            const uint32_t framesInFlight = Engine::FrameManager::GetInstance().GetMaxFramesInFlight();
            return framesInFlight > 0 ? framesInFlight : 3u;
        }

        // Returns true at most once per absolute frame for a given tracker. Updates the tracker to the
        // current frame when it allows the clean to proceed.
        inline bool ConsumeCleanThisFrame( uint64_t& lastCleanFrame )
        {
            const uint64_t current = Engine::FrameManager::GetInstance().GetAbsoluteFrameCount();
            if ( lastCleanFrame == current )
                return false;
            lastCleanFrame = current;
            return true;
        }
    } // namespace PropertyDirty
} // namespace Desert::Graphic
