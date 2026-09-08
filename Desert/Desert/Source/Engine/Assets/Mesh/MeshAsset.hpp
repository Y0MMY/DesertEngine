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

        // A SINGULAR `GetMaterialHandle( submeshIndex )` STOOD BESIDE THIS ONE and Г12 removed it, along
        // with the two helpers that existed only to serve it: a shared bounds check and a
        // `NullMaterialHandle()` constant. All three had zero callers; every caller in the engine and the
        // editor uses the plural below.
        //
        // The check is worth a sentence, because deleting a guard deserves an argument rather than a
        // shrug. It was added after a real defect — both implementations indexed the vector with no bound
        // and one never populated it — and its error path named the mesh, the index and the size. But two
        // facts make it unreachable rather than merely unused: all five callers of the plural accessor
        // iterate it or take its size, none indexes with a bare `[i]`; and the loaders build exactly one
        // handle per submesh from the same parsed data, so the two sizes agree BY CONSTRUCTION after any
        // successful load. A guard against a state the constructor cannot produce, reached through a
        // function nobody calls, is not safety — it is the appearance of it.
        //
        // If a per-index accessor is ever wanted again, it needs that bounds check back: the reason it
        // was written has not stopped being true, only stopped being reachable.
        virtual const std::vector<Common::UUID>& GetMaterialHandles() const = 0;
        virtual bool                             IsSkinned() const          = 0;

        // THE DRAWABLE PARTS OF THIS MESH — on the base, because the one thing every caller of the mesh
        // services needs to know about a mesh asset is how many pieces it has, and until now that question
        // could only be asked of a *concrete* type. Both subclasses already had this exact signature; only
        // the base did not, so `MeshService::Register` could not compare what it BUILT against what the
        // asset HOLDS and shipped a mesh with zero submeshes built from an unparsed shell. See
        // MeshService::BuildAndCache for the relation this makes expressible.
        virtual const std::vector<Submesh>& GetSubmeshes() const = 0;

        // Blendshapes for this mesh (empty when it has none). Overridden by Static/SkinnedMeshAsset; the base
        // default lets any MeshAsset* be queried uniformly (e.g. the Details morph widget).
        virtual const std::vector<MorphTarget>& GetMorphTargets() const
        {
            static const std::vector<MorphTarget> kEmpty;
            return kEmpty;
        }
    };

} // namespace Desert::Assets