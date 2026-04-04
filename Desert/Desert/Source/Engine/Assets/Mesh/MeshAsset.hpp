#pragma once

#include <Engine/Assets/AssetBase.hpp>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/AssetEvents.hpp>

namespace Desert::Assets
{
    class MeshAsset : public AssetBase, public AssetsEventSystem
    {
    public:
        using AssetBase::AssetBase;
        virtual ~MeshAsset() = default;

        static AssetTypeID GetTypeID()
        {
            return AssetTypeID::Mesh;
        }

        virtual bool IsSkinned() const = 0;
    };

} // namespace Desert::Assets