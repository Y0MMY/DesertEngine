#pragma once

#include <Engine/Assets/Common.hpp>

namespace Desert::Assets
{
    struct MeshAssetBundle
    {
        AssetMetadata MeshMetadata;
        AssetMetadata MaterialMetadata;

        bool IsValid() const
        {
            return MeshMetadata.IsValid() && MaterialMetadata.IsValid();
        }
    };
} // namespace Desert::Assets