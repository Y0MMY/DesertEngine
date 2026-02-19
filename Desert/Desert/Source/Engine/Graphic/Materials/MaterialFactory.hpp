#pragma once

#include <Engine/Graphic/Materials/Mesh/PBR/StaticMaterialPBR.hpp>
#include <Engine/Graphic/Materials/Skybox/MaterialSkybox.hpp>

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Mesh/MaterialAsset.hpp>

namespace Desert::Graphic
{
    class MaterialFactory
    {
    public:
        static std::shared_ptr<StaticMaterialPBR> CreatePBR( const std::shared_ptr<Assets::MaterialAsset>& baseAsset );
        static std::shared_ptr<MaterialSkybox> CreateSkybox( const std::shared_ptr<Assets::TextureAsset>& baseAsset );
    };
} // namespace Desert::Graphic