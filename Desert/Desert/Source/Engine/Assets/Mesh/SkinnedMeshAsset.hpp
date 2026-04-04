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

        const std::vector<Submesh>& GetSubmeshes() const
        {
            return m_Submeshes;
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
                LOG_ERROR( "Failed to resolve SkeletonAsset dependency." );
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

        uint64_t m_SkeletonSignature = 0U;

        // Dependency
        AssetDependency<SkeletonAsset> m_SkeletonDependency;

        bool m_IsReadyForUse = false;
    };
} // namespace Desert::Assets