#pragma once

#include <Engine/Graphic/Materials/MaterialAssetLink.hpp>

namespace Desert::Graphic
{
    class MaterialFactory
    {
    public:
        static MaterialAssetLink CreateFromAsset( const Assets::Asset<Assets::MaterialAsset>& asset );
    };
} // namespace Desert::Graphic