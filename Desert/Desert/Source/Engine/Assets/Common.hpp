#pragma once

#include <Common/Core/AssetHandle.hpp>

namespace Desert::Assets
{
    using AssetHandle = Common::AssetHandle;

    template <typename T>
    using Asset = std::shared_ptr<T>;

    using NullAsset = nullptr_t;

    enum class AssetPriority
    {
        Low    = 0,
        Medium = 1,
        High   = 2,
    };

    enum class AssetTypeID
    {
        Unknown = 0,
        Mesh,
        Material,
        Texture2D,
        Skybox,
        Shader,
        Skeleton,
        Animation,
        Prefab,
        // The volumetric clouds' 3D noise (`.dcnv`). A first-class asset rather than a bake output, so an
        // artist can author several and drop one into the cloud component's slot — see
        // Engine/Assets/CloudNoiseVolume.hpp.
        CloudNoiseVolume,
    };
} // namespace Desert::Assets