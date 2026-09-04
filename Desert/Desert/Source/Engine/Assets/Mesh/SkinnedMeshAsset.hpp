#pragma once

#include "MeshAsset.hpp"

#include <Engine/Geometry/MeshTypes.hpp>
#include <Engine/Assets/Common.hpp>
#include <Engine/Assets/Mesh/SkeletonAsset.hpp>

#include <optional>
#include <vector>

namespace Desert::Assets
{
    class SkinnedMeshAsset final : public MeshAsset
    {
    public:
        SkinnedMeshAsset( const AssetPriority priority, const Common::Filepath& filepath );

        // -------------------------------------------------
        // Asset lifecycle
        // -------------------------------------------------
        Common::BoolResultStr Load() override;
        Common::BoolResultStr Unload() override;

        bool IsSkinned() const override
        {
            return true;
        }

        // -------------------------------------------------
        // Accessors (CPU data only)
        // -------------------------------------------------
        const std::vector<SkinnedVertex>& GetVertices() const
        {
            return m_Vertices;
        }

        const std::vector<Index>& GetIndices() const
        {
            return m_Indices;
        }

        virtual const Common::UUID& GetMaterialHandle( const uint32_t submeshIndex ) const override
        {
            return m_MaterialAssetHandles[submeshIndex];
        }

        virtual const std::vector<Common::UUID>& GetMaterialHandles() const override
        {
            return m_MaterialAssetHandles;
        }

        const std::vector<Submesh>& GetSubmeshes() const
        {
            return m_Submeshes;
        }

        // Blendshapes (empty when the mesh has none). Deltas are index-aligned with GetVertices().
        const std::vector<MorphTarget>& GetMorphTargets() const
        {
            return m_MorphTargets;
        }

        const auto& GetSkeletonDependency() const
        {
            return m_SkeletonDependency;
        }

        virtual void ResolveDependencies( AssetManager& manager ) override
        {
            // Re-runnable by construction: the previous answer is dropped first, so a second call after the
            // file is parsed cannot leave a stale binding behind and cannot be mistaken for the first.
            m_SkeletonDependency.Handle = Common::AssetHandle::Null();
            m_SkeletonDependency.Cached.reset();

            // A SIGNATURE OF ZERO MEANS "NOT KNOWN YET", NEVER "MATCHES ANYTHING". The signature is a field
            // inside the .skmesh, so an unparsed shell carries 0 — and SkeletonAsset::GetSignature() also
            // returns 0 for a skeleton whose own file has not been read. Comparing the two would bind this
            // mesh to the first unloaded skeleton in the project and report the dependency as resolved,
            // which is a worse failure than the unresolved one because nothing downstream can detect it.
            // AssetBase::EnsureLoaded runs this again the moment the parse fills the signature in.
            if ( m_SkeletonSignature == 0 )
            {
                return;
            }

            const auto& allSkeletons = manager.FindAllByType<Assets::SkeletonAsset>();
            for ( const auto& [handle, skeleton] : allSkeletons )
            {
                if ( skeleton->GetSignature() == m_SkeletonSignature )
                {
                    m_SkeletonDependency.Handle = handle;
                    m_SkeletonDependency.Cached = skeleton;
                    break;
                }
            }

            if ( !m_SkeletonDependency.IsValid() )
            {
                LOG_WARN( "SkinnedMeshAsset '{}': skeleton sig {} not found among {} skeletons.",
                          m_Metadata.Filepath.string(), m_SkeletonSignature, allSkeletons.size() );
            }
        }

        // The rig this mesh was cooked against, as stored in the .skmesh. Zero until the file is parsed.
        uint64_t GetSkeletonSignature() const
        {
            return m_SkeletonSignature;
        }

        bool IsReadyForUse() const override
        {
            return m_IsReadyForUse;
        }

    private:
        // Skinning geometry
        std::vector<SkinnedVertex> m_Vertices;
        std::vector<Index>         m_Indices;
        std::vector<Submesh>       m_Submeshes;
        std::vector<MorphTarget>   m_MorphTargets;
        std::vector<Common::UUID>  m_MaterialAssetHandles;

        uint64_t m_SkeletonSignature = 0U;

        // Dependency
        AssetDependency<SkeletonAsset> m_SkeletonDependency;

        bool m_IsReadyForUse = false;
    };
} // namespace Desert::Assets