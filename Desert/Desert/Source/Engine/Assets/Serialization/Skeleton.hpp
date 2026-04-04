#pragma once

#include <Engine/Animation/Skeleton.hpp>

namespace Desert::Assets::Serialization
{
    struct SkeletonAssetData
    {
        uint64_t                                 Signature;
        std::vector<Desert::Animation::BoneInfo> Bones;
    };
} // namespace Desert::Assets::Serialization