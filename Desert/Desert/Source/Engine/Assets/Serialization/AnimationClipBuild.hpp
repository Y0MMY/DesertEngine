#pragma once

#include <Common/Core/ResultStr.hpp>

#include <Engine/Animation/AnimationClip.hpp>
#include <Engine/Assets/Serialization/Animation.hpp>

namespace Desert::Assets::Serialization
{
    /**
     * @brief The `.anim` file's channel list -> the runtime clip. PURE: no files, no GPU, no globals, so the
     *        rules below are testable without an AssetManager (AnimationClipFormat suite).
     *
     * IT REFUSES INSTEAD OF PRODUCING A CLIP THAT ANIMATES NOTHING. Two shapes are rejected by name:
     *
     *  - a channel with an EMPTY bone name. The name is the only key playback binds on
     *    (Animator::ResolveTrack), so such a channel is keys nobody will ever sample. It used to be
     *    accepted, land at whatever slot the channel's serialised bone index pointed at, and disappear.
     *  - TWO channels claiming the same bone. `ResolveTrack` builds a name -> track map, so the second
     *    silently won and the first one's keys were dropped — a middle link losing a property, with both
     *    ends looking correct.
     *
     * The old loader also sized the track vector from `max(channel.BoneIndex) + 1`, which made a corrupt or
     * indeterminate index an unbounded allocation. There is no index any more; tracks are the channels, in
     * file order.
     */
    [[nodiscard]] Common::ResultStr<Animation::AnimationClip>
    BuildClipFromAssetData( const AnimationAssetData& data );
} // namespace Desert::Assets::Serialization
