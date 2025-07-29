#pragma once

namespace Desert::Assets
{
    using AssetHandle = Common::UUID;

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
        Skybox
    };
} // namespace Desert::Assets