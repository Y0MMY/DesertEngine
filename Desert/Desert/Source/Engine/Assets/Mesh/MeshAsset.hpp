#pragma once

#include <Engine/Assets/AssetBase.hpp>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/AssetEvents.hpp>
#include <Engine/Geometry/MeshTypes.hpp>

#include <vector>

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

        virtual const Common::UUID&              GetMaterialHandle( const uint32_t submeshIndex ) const = 0;
        virtual const std::vector<Common::UUID>& GetMaterialHandles() const                             = 0;
        virtual bool                             IsSkinned() const                                      = 0;

        // Blendshapes for this mesh (empty when it has none). Overridden by Static/SkinnedMeshAsset; the base
        // default lets any MeshAsset* be queried uniformly (e.g. the Details morph widget).
        virtual const std::vector<MorphTarget>& GetMorphTargets() const
        {
            static const std::vector<MorphTarget> kEmpty;
            return kEmpty;
        }
    };

} // namespace Desert::Assets