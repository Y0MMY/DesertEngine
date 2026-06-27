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

        static AssetTypeID GetTypeID()
        {
            return AssetTypeID::Material;
        }

        virtual Common::UUID GetMaterialUUID() const = 0;

        // Name of the shader program this material drives (e.g. "StaticMeshPBR", "Unlit"). MaterialFactory
        // routes it: specialized shaders (PBR) get their C++ material, everything else a generic
        // DataDrivenMaterial. Replaces the old closed `MaterialType` enum.
        virtual std::string GetShaderName() const = 0;
    };

} // namespace Desert::Assets