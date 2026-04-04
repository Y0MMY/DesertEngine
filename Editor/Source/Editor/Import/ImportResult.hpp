#pragma once

#include <vector>
#include <optional>
#include <string>

#include <Engine/Assets/Serialization/Mesh.hpp>
#include <Engine/Assets/Serialization/Animation.hpp>
#include <Engine/Assets/Serialization/Skeleton.hpp>

namespace Desert::Editor
{
    struct ImportResult
    {
        std::optional<Desert::Assets::Serialization::MeshAssetData>     Mesh;
        std::optional<Desert::Assets::Serialization::SkeletonAssetData> Skeleton;
        std::vector<Desert::Assets::Serialization::AnimationAssetData>  Animations;
    };
} // namespace Desert::Editor