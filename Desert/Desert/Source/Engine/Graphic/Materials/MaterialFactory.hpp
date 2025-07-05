#pragma once

#include <Engine/Graphic/Materials/MaterialPBR.hpp>
#include <Engine/Graphic/Materials/MaterialSkybox.hpp>
#include <Engine/Assets/AssetManager.hpp>

namespace Desert::Graphic
{
    class MaterialFactory
    {
    public:
        static std::shared_ptr<MaterialPBR> CreatePBR( const std::shared_ptr<Assets::MaterialAsset>& baseAsset );
        static std::shared_ptr<MaterialSkybox> CreateSkybox( const std::shared_ptr<Assets::TextureAsset>& baseAsset );
    };
} // namespace Desert::Graphic