#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Assets/Mesh/MaterialAsset.hpp>

namespace Desert::Graphic
{
    struct MaterialAssetLink
    {
        std::shared_ptr<Material> MaterialInstance;
        Assets::AssetHandle       SourceAsset;
    };
} // namespace Desert::Graphic
