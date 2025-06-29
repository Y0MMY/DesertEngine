#pragma once

#include <Engine/Graphic/Materials/MaterialInstance.hpp>
#include <Engine/Assets/AssetManager.hpp>

namespace Desert::Graphic
{
    class MaterialFactory
    {
    public:
        static std::unique_ptr<MaterialInstance> Create( const std::shared_ptr<Assets::MaterialAsset>& baseAsset );
    };
} // namespace Desert::Graphic