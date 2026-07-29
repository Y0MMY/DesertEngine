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
            const auto& allSKeletons = manager.FindAllByType<Assets::SkeletonAsset>();
            for ( const auto& skeleton : allSKeletons )
            {
                if ( skeleton.second->GetSignature() == m_SkeletonSignature )
                {
                    m_SkeletonDependency.Cached = skeleton.second;
                }
            }

            if ( !m_SkeletonDependency.IsValid() )
            {
                // Common + harmless during preload: a lazy mesh shell hasn't loaded its signature yet (it's 0)
                // — it gets re-resolved when finalized (drop/first use). Only a non-zero, still-unresolved
                // signature is a real mismatch.
                if ( m_SkeletonSignature != 0 )
                    LOG_WARN( "SkinnedMeshAsset '{}': skeleton sig {} not found among {} skeletons.",
                              m_Metadata.Filepath.string(), m_SkeletonSignature, allSKeletons.size() );
            }
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