#pragma once

#include <Engine/Animation/Skeleton.hpp>

namespace Desert::Assets::Serialization
{
    struct SkeletonAssetData
    {
        // Initialised for the same reason as AnimationAssetData's scalars: this struct IS the .skeleton file,
        // and 0 is the value SkinnedMeshAsset already reads as "no rig claimed".
        uint64_t                                 Signature = 0;
        std::vector<Desert::Animation::BoneInfo> Bones;
    };
} // namespace Desert::Assets::Serialization