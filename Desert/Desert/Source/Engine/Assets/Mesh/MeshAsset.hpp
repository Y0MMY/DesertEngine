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

        // The material a submesh names, or the NULL handle when there is none — which is also what an
        // out-of-range index answers, loudly. Both implementations used to `return m_MaterialAssetHandles[
        // submeshIndex ]` with no bound at all, and one of them never populated the vector, so the read was
        // out of bounds every time. `MeshAsset::NullMaterialHandle()` is the value both refusals return, so
        // "no material" has one spelling.
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

        /// The one value that means "this submesh names no material". A reference, because
        /// GetMaterialHandle returns one and a refusal has to be able to.
        static const Common::UUID& NullMaterialHandle()
        {
            static const Common::UUID kNone = Common::UUID::Null();
            return kNone;
        }

        /// The bounds check both implementations share, so a second copy cannot disagree with the first.
        /// Names the mesh, the index and the size — a bare null would be indistinguishable from a submesh
        /// that genuinely has no material, which is §1.4's rule applied to a lookup.
        const Common::UUID& MaterialHandleAt( const std::vector<Common::UUID>& handles,
                                              const uint32_t                   submeshIndex ) const
        {
            if ( submeshIndex < handles.size() )
                return handles[submeshIndex];

            LOG_ERROR( "[Mesh] '{}' was asked for the material of submesh {} and carries {} material "
                       "handle(s). The submesh draws with the default material. This means the mesh's "
                       "submeshes and its material list disagree — usually a mesh cooked before the "
                       "importer wrote per-submesh materials; re-cook it (Assets > Rebuild Cooked Assets).",
                       m_Metadata.Filepath.string(), submeshIndex, handles.size() );
            return NullMaterialHandle();
        }
    };

} // namespace Desert::Assets