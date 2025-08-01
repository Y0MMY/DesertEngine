#pragma once

#include <Engine/Graphic/Materials/Mesh/PBR/MaterialPBR.hpp>
#include <Engine/Graphic/Materials/Skybox/MaterialSkybox.hpp>
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