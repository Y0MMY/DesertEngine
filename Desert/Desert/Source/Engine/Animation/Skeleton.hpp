#pragma once

#include <Engine/Geometry/Mesh.hpp>
#include <Engine/Animation/AnimationClip.hpp>
#include <Engine/Animation/BoneInfo.hpp>

#include <functional>
#include <vector>

namespace Desert::Animation
{
    class Skeleton
    {
    public:
        explicit Skeleton( std::vector<BoneInfo>&& bones ) : m_Bones( std::move( bones ) )
        {
            m_Signature = ComputeSignature( m_Bones );
        }

        const std::vector<BoneInfo>& GetBones() const
        {
            return m_Bones;
        }

        // Finds a bone by name (case-sensitive). The returned index matches the Pose::BoneMatrices index, so
        // sockets/attachments can look up "hand_r" once and then read its matrix from the animator's pose.
        [[nodiscard]] std::optional<uint32_t> FindBoneIndex( const std::string& name ) const
        {
            for ( size_t i = 0; i < m_Bones.size(); ++i )
            {
                if ( m_Bones[i].Name == name )
                    return static_cast<uint32_t>( i );
            }
            return std::nullopt;
        }

        // Mutable access for the in-editor skeleton editor (rest-pose editing). The signature is intentionally
        // NOT recomputed on edit so the SkinnedMeshAsset->SkeletonAsset dependency link (matched by signature)
        // stays intact. After changing LocalBindTransforms, call RecomputeOffsetMatrices().
        std::vector<BoneInfo>& GetBonesMutable()
        {
            return m_Bones;
        }

        // Rebuilds every bone's OffsetMatrix (= inverse of its global bind pose) from the current
        // LocalBindTransform chain, so edited rest-pose bones stay consistent with skinning. Memoized parent
        // resolve, so bone array order (parent vs child) doesn't matter.
        void RecomputeOffsetMatrices()
        {
            std::vector<glm::mat4> global( m_Bones.size(), glm::mat4( 1.0f ) );
            std::vector<bool>      done( m_Bones.size(), false );
            std::function<glm::mat4( size_t )> resolve = [&]( size_t i ) -> glm::mat4
            {
                if ( done[i] )
                    return global[i];
                glm::mat4 g = m_Bones[i].LocalBindTransform;
                if ( m_Bones[i].ParentBoneID.has_value() && m_Bones[i].ParentBoneID.value() < m_Bones.size() )
                    g = resolve( m_Bones[i].ParentBoneID.value() ) * m_Bones[i].LocalBindTransform;
                global[i] = g;
                done[i]   = true;
                return g;
            };
            for ( size_t i = 0; i < m_Bones.size(); ++i )
                m_Bones[i].OffsetMatrix = glm::inverse( resolve( i ) );
        }

        uint64_t GetSignature() const
        {
            return m_Signature;
        }

        static uint64_t ComputeSignature( const std::vector<BoneInfo>& bones );

    private:
        std::vector<BoneInfo> m_Bones;
        uint64_t              m_Signature;
    };
} // namespace Desert::Animation
