#pragma once

#include <Engine/Assets/MaterialAsset.hpp>

#include <Engine/Graphic/Materials/Material.hpp>

namespace Desert::Graphic
{
    class MaterialFactory
    {
    public:
        static std::shared_ptr<Material> Create( const std::shared_ptr<Assets::MaterialAsset>& asset );
    };
} // namespace Desert::Graphic