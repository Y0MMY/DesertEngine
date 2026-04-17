#pragma once

#include <Engine/Assets/AssetBase.hpp>
#include <Engine/Graphic/Texture.hpp>

#include <Engine/Assets/AssetManager.hpp>

namespace Desert::Assets
{
    class MaterialAsset : public AssetBase
    {
    public:
        using AssetBase::AssetBase;

        enum class MaterialType
        {
            PBR,
            SkinnedPBR,
            Skybox,
            Unlit,
            PostProcess
        };

        static AssetTypeID GetTypeID()
        {
            return AssetTypeID::Material;
        }

        virtual Common::UUID GetMaterialUUID() const = 0;
        virtual MaterialType GetMaterialType() const = 0;
    };

} // namespace Desert::Assets