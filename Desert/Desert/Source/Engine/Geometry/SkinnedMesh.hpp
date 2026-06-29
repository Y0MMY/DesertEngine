#pragma once

#include "Mesh.hpp"

namespace Desert::Animation
{
    class Skeleton;
    struct BoneInfo;
} // namespace Desert::Animation

namespace Desert
{

    class SkinnedMesh : public Mesh
    {
    public:
        SkinnedMesh( const std::vector<SkinnedVertex>& vertices, const std::vector<Index>& indices,
                     const std::vector<Submesh>& submeshes, const Animation::Skeleton* skeleton );

        MeshType GetType() const override
        {
            return MeshType::Skinned;
        }

        const Animation::Skeleton& GetSkeleton() const
        {
            return *m_Skeleton;
        }

        // Mutable skeleton for the in-editor skeleton editor. The underlying Skeleton is owned (non-const) by
        // its SkeletonAsset and merely referenced here as const*, so the cast is safe. Edits affect every
        // skinned mesh sharing this rig (correct — a rig is shared).
        Animation::Skeleton* GetSkeletonMutable() const
        {
            return const_cast<Animation::Skeleton*>( m_Skeleton );
        }

        [[nodiscard]] virtual Common::BoolResultWithCodes<MeshError> Invalidate() override;

    private:
        const Animation::Skeleton*                m_Skeleton;
        std::unordered_map<std::string, uint32_t> m_BoneNameToIndex;
    };
} // namespace Desert
